/*  FlexNBD server (C) Bytemark Hosting 2012

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "mirror.h"
#include "serve.h"
#include "util.h"
#include "ioutil.h"
#include "parse.h"
#include "readwrite.h"
#include "bitset.h"
#include "self_pipe.h"
#include "status.h"


#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <unistd.h>

struct mirror * mirror_alloc(
		union mysockaddr * connect_to,
		union mysockaddr * connect_from,
		int max_Bps,
		int action_at_finish,
		struct mbox * commit_signal)
{
	struct mirror * mirror;

	mirror = xmalloc(sizeof(struct mirror));
	mirror->connect_to = connect_to;
	mirror->connect_from = connect_from;
	mirror->max_bytes_per_second = max_Bps;
	mirror->action_at_finish = action_at_finish;
	mirror->commit_signal = commit_signal;
	mirror->commit_state = MS_UNKNOWN;

	return mirror;
}

void mirror_set_state_f( struct mirror * mirror, enum mirror_state state )
{
	NULLCHECK( mirror );
	mirror->commit_state = state;
}

#define mirror_set_state( mirror, state ) do{\
	debug( "Mirror state => " #state );\
	mirror_set_state_f( mirror, state );\
} while(0)

enum mirror_state mirror_get_state( struct mirror * mirror )
{
	NULLCHECK( mirror );
	return mirror->commit_state;
}


void mirror_init( struct mirror * mirror, const char * filename )
{
	int map_fd;
	off64_t size;

	NULLCHECK( mirror );
	NULLCHECK( filename );

	FATAL_IF_NEGATIVE(
		open_and_mmap(
			filename, 
			&map_fd,
			&size, 
			(void**) &mirror->mapped
		),
		"Failed to open and mmap %s",
		filename
	);
	
	mirror->dirty_map = bitset_alloc(size, 4096);

}


/* Call this before a mirror attempt. */
void mirror_reset( struct mirror * mirror )
{
	NULLCHECK( mirror );
	NULLCHECK( mirror->dirty_map );
	mirror_set_state( mirror, MS_INIT );
	bitset_set(mirror->dirty_map);
}


struct mirror * mirror_create(
		const char * filename,
		union mysockaddr * connect_to,
		union mysockaddr * connect_from,
		int max_Bps,
		int action_at_finish,
		struct mbox * commit_signal)
{
	/* FIXME: shouldn't map_fd get closed? */
	struct mirror * mirror;

	mirror = mirror_alloc( connect_to,
			connect_from,
			max_Bps,
			action_at_finish,
			commit_signal);
	
	mirror_init( mirror, filename );
	mirror_reset( mirror );


	return mirror;
}


void mirror_destroy( struct mirror *mirror )
{
	NULLCHECK( mirror );
	free(mirror->connect_to);
	free(mirror->connect_from);
	free(mirror->dirty_map);
	free(mirror);
}


/** The mirror code will split NBD writes, making them this long as a maximum */
static const int mirror_longest_write = 8<<20;

/** If, during a mirror pass, we have sent this number of bytes or fewer, we
 *  go to freeze the I/O and finish it off.  This is just a guess.
 */
static const unsigned int mirror_last_pass_after_bytes_written = 100<<20;

/** The largest number of full passes we'll do - the last one will always 
 *  cause the I/O to freeze, however many bytes are left to copy.
 */
static const int mirror_maximum_passes = 7;

/* A single mirror pass over the disc, optionally locking IO around the
 * transfer.
 */
int mirror_pass(struct server * serve, int should_lock, uint64_t *written)
{
	uint64_t current = 0;
	int success = 1;
	struct bitset_mapping *map = serve->mirror->dirty_map;
	*written = 0;

	while (current < serve->size) {
		int run = bitset_run_count(map, current, mirror_longest_write);

		debug("mirror current=%ld, run=%d", current, run);

		/* FIXME: we could avoid sending sparse areas of the 
		 * disc here, and probably save a lot of bandwidth and
		 * time (if we know the destination starts off zeroed).
		 */ 
		if (bitset_is_set_at(map, current)) {
			/* We've found a dirty area, send it */
			debug("^^^ writing");

			/* We need to stop the main thread from working 
			 * because it might corrupt the dirty map.  This
			 * is likely to slow things down but will be
			 * safe.
			 */
			if (should_lock) { server_lock_io( serve ); }
			{
				debug("in lock block");
				/** FIXME: do something useful with bytes/second */

				/** FIXME: error handling code here won't unlock */
				socket_nbd_write( serve->mirror->client, 
						current,
						run,
						0,
						serve->mirror->mapped + current,
						MS_REQUEST_LIMIT_SECS);

				/* now mark it clean */
				bitset_clear_range(map, current, run);
				debug("leaving lock block");
			}
			if (should_lock) { server_unlock_io( serve ); }

			*written += run;
		}
		current += run;

		if (serve->mirror->signal_abandon) {
			debug("Abandon message received" );
			success = 0;
			break;
		}
	}

	return success;
}


void mirror_give_control( struct mirror * mirror )
{
	debug( "mirror: entrusting and disconnecting" );
	/* TODO: set up an error handler to clean up properly on ERROR.
	 */

	/* A transfer of control is expressed as a 3-way handshake.
	 * First, We send a REQUEST_ENTRUST. If this fails to be
	 * received, this thread will simply block until the server is
	 * restarted. If the remote end doesn't understand it, it'll
	 * disconnect us, and an ERROR *should* bomb this thread.
	 * FIXME: make the ERROR work.
	 * If we get an explicit error back from the remote end, then
	 * again, this thread will bomb out.
	 * On receiving a valid response, we send a REQUEST_DISCONNECT,
	 * and we quit without checking for a response.  This is the
	 * remote server's signal to assume control of the file.  The
	 * reason we don't check for a response is the state we end up
	 * in if the final message goes astray: if we lose the
	 * REQUEST_DISCONNECT, the sender has quit and the receiver
	 * hasn't had a signal to take over yet, so the data is safe.
	 * If we were to wait for a response to the REQUEST_DISCONNECT,
	 * the sender and receiver would *both* be servicing write
	 * requests while the response was in flight, and if the
	 * response went astray we'd have two servers claiming
	 * responsibility for the same data.
	 *
	 * The meaning of these is as follows:
	 * The entrust signifies that all the data has been sent, and
	 * the client is currently paused but not disconnected.
	 * The disconnect signifies that the client has been
	 * safely prevented from making any more writes.
	 *
	 * Since we lock io and close the server it in mirror_on_exit before
	 * releasing, we don't actually need to take any action between the
	 * two here.
	 */
	socket_nbd_entrust( mirror->client );
	socket_nbd_disconnect( mirror->client );
}


/* THIS FUNCTION MUST ONLY BE CALLED WITH THE SERVER'S IO LOCKED. */
void mirror_on_exit( struct server * serve )
{
	/* Send an explicit entrust and disconnect. After this
	 * point we cannot allow any reads or writes to the local file.
	 * We do this *before* trying to shut down the server so that if
	 * the transfer of control fails, we haven't stopped the server
	 * and already-connected clients don't get needlessly
	 * disconnected.
	 */
	debug( "mirror_give_control");
	mirror_give_control( serve->mirror );

	/* If we're still here, the transfer of control went ok, and the
	 * remote is listening (or will be shortly).  We can shut the
	 * server down.
	 *
	 * It doesn't matter if we get new client connections before
	 * now, the IO lock will stop them from doing anything.
	 */
	debug("serve_signal_close");
	serve_signal_close( serve );

	/* We have to wait until the server is closed before unlocking
	 * IO.  This is because the client threads check to see if the
	 * server is still open before reading or writing inside their
	 * own locks. If we don't wait for the close, there's no way to
	 * guarantee the server thread will win the race and we risk the
	 * clients seeing a "successful" write to a dead disc image.
	 */
	debug("serve_wait_for_close");
	serve_wait_for_close( serve );
	info("Mirror sent.");
}


void mirror_cleanup( struct server * serve,
		int fatal __attribute__((unused)))
{
	NULLCHECK( serve );
	struct mirror * mirror = serve->mirror;
	NULLCHECK( mirror );
	info( "Cleaning up mirror thread");

	if( mirror->client && mirror->client > 0 ){
		close( mirror->client );
	}
	mirror->client = -1;

	if( server_io_locked( serve ) ){ server_unlock_io( serve ); }
}



int mirror_connect( struct mirror * mirror, off64_t local_size )
{
	struct sockaddr * connect_from = NULL;
	int connected =  0;

	if ( mirror->connect_from ) {
		connect_from = &mirror->connect_from->generic;
	}

	NULLCHECK( mirror->connect_to );

	mirror->client = socket_connect(&mirror->connect_to->generic, connect_from);
	if ( 0 < mirror->client ) {
		fd_set fds;
		struct timeval tv = { MS_HELLO_TIME_SECS, 0};
		FD_ZERO( &fds );
		FD_SET( mirror->client, &fds );

		FATAL_UNLESS( 0 <= select( FD_SETSIZE, &fds, NULL, NULL, &tv ),
				"Select failed." );

		if( FD_ISSET( mirror->client, &fds ) ){
			off64_t remote_size;
			if ( socket_nbd_read_hello( mirror->client, &remote_size ) ) {
				if( remote_size == local_size ){
					connected = 1;
					mirror_set_state( mirror, MS_GO );
				}
				else {
					warn("Remote size (%d) doesn't match local (%d)", 
							remote_size, local_size );
					mirror_set_state( mirror, MS_FAIL_SIZE_MISMATCH );
				}
			}
			else {
				warn( "Mirror attempt rejected." );
				mirror_set_state( mirror, MS_FAIL_REJECTED );
			}
		}
		else {
			warn( "No NBD Hello received." );
			mirror_set_state( mirror, MS_FAIL_NO_HELLO );
		}

		if ( !connected ) { close( mirror->client ); }
	}
	else {
		warn( "Mirror failed to connect.");
		mirror_set_state( mirror, MS_FAIL_CONNECT );
	}

	return connected;
}



void mirror_run( struct server *serve )
{
	NULLCHECK( serve );
	NULLCHECK( serve->mirror );

	int pass;
	uint64_t written;

	info("Starting mirror" );
	for (pass=0; pass < mirror_maximum_passes-1; pass++) {

		debug("mirror start pass=%d", pass);
		if ( !mirror_pass( serve, 1, &written ) ){ 
			debug("Failed mirror pass state is %d", mirror_get_state( serve->mirror ) );
			debug("pass failed, giving up");
			return; }
		
		/* if we've not written anything */
		if (written < mirror_last_pass_after_bytes_written) { break; }
	}

	server_lock_io( serve );
	{
		if ( mirror_pass( serve, 0, &written ) &&
				ACTION_EXIT == serve->mirror->action_at_finish) {
			debug("exit!");
			mirror_on_exit( serve );
			info("Server closed, quitting "
					"after successful migration");
		} 
	}
	server_unlock_io( serve );
}


void mbox_post_mirror_state( struct mbox * mbox, enum mirror_state st )
{
	NULLCHECK( mbox );
	enum mirror_state * contents = xmalloc( sizeof( enum mirror_state ) );

	*contents = st;

	mbox_post( mbox, contents );
}


void mirror_signal_commit( struct mirror * mirror )
{
	NULLCHECK( mirror );

	mbox_post_mirror_state( mirror->commit_signal, 
			mirror_get_state( mirror ) );
}

/** Thread launched to drive mirror process 
 * This is needed for two reasons: firstly, it decouples the mirroring
 * from the control thread (although that's less valid with mboxes
 * passing state back and forth) and to provide an error context so that
 * retries can be cleanly handled without a bespoke error handling
 * mechanism.
 * */
void* mirror_runner(void* serve_params_uncast)
{
	/* The supervisor thread relies on there not being any ERROR
	 * calls until after the mirror_signal_commit() call in this
	 * function.
	 * However, *after* that, we should call ERROR_* instead of
	 * FATAL_* wherever possible.
	 */
	struct server *serve = (struct server*) serve_params_uncast;

	NULLCHECK( serve );
	NULLCHECK( serve->mirror );
	struct mirror * mirror = serve->mirror;
	NULLCHECK( mirror->dirty_map );

	error_set_handler( (cleanup_handler *) mirror_cleanup, serve );

	info( "Connecting to mirror" );
	
	time_t start_time = time(NULL);
	int connected = mirror_connect( mirror, serve->size );
	mirror_signal_commit( mirror );
	if ( !connected ) { goto abandon_mirror; }

	/* After this point, if we see a failure we need to disconnect
	 * and retry everything from mirror_set_state(_, MS_INIT), but
	 * *without* signaling the commit or abandoning the mirror.
	 * */
	
	if ( (time(NULL) - start_time) > MS_CONNECT_TIME_SECS ){ 
		/* If we get here, then we managed to connect but the
		 * control thread feeding status back to the user will
		 * have gone away, leaving the user without meaningful
		 * feedback. In this instance, they have to assume a
		 * failure, so we can't afford to let the mirror happen.
		 * We have to set the state to avoid a race.
		 */
		mirror_set_state( mirror, MS_FAIL_CONNECT );
		warn( "Mirror connected, but too slowly" );
		goto abandon_mirror;
	}

	mirror_run( serve );

	mirror_set_state( mirror, MS_DONE );
abandon_mirror:
	return NULL;
}


struct mirror_super * mirror_super_create(
		const char * filename,
		union mysockaddr * connect_to,
		union mysockaddr * connect_from,
		int max_Bps,
		int action_at_finish)
{
	struct mirror_super * super = xmalloc( sizeof( struct mirror_super) );
	super->mirror = mirror_create( 
			filename, 
			connect_to, 
			connect_from, 
			max_Bps, 
			action_at_finish,
			mbox_create() ) ;
	super->state_mbox = mbox_create();
	return super;
}


/* Post the current state of the mirror into super->state_mbox */
void mirror_super_signal_committed( 
		struct mirror_super * super ,
		enum mirror_state commit_state )
{
	NULLCHECK( super );
	NULLCHECK( super->state_mbox );

	mbox_post_mirror_state( 
			super->state_mbox, 
			commit_state );
}


void mirror_super_destroy( struct mirror_super * super )
{
	NULLCHECK( super );

	mbox_destroy( super->state_mbox );
	mbox_destroy( super->mirror->commit_signal );
	mirror_destroy( super->mirror );
	free( super );
}


/* The mirror supervisor thread.  Responsible for kicking off retries if
 * the mirror thread fails.
 * The mirror and mirror_super objects are never freed, and the
 * mirror_super_runner thread is never joined.
 */
void * mirror_super_runner( void * serve_uncast )
{
	struct server * serve = (struct server *) serve_uncast;
	NULLCHECK( serve );
	NULLCHECK( serve->mirror );
	NULLCHECK( serve->mirror_super );

	int first_pass = 1;
	int should_retry = 0;
	int success = 0;

	struct mirror * mirror = serve->mirror;
	struct mirror_super *  super  = serve->mirror_super;

	do {
		FATAL_IF( 0 != pthread_create(
					&mirror->thread, 
					NULL, 
					mirror_runner, 
					serve),
				"Failed to create mirror thread");

		debug("Supervisor waiting for commit signal");
		enum mirror_state * commit_state = 
			mbox_receive( mirror->commit_signal );

		debug( "Supervisor got commit signal" );
		if ( first_pass ) {
			/* Only retry if the connection attempt was
			 * successful.  Otherwise the user will see an
			 * error reported while we're still trying to
			 * retry behind the scenes.
			 */
			should_retry = *commit_state == MS_GO;
			/* Only send this signal the first time */
			mirror_super_signal_committed(
					super, 
					*commit_state);
			debug("Mirror supervisor committed");
		}
		/* We only care about the value of the commit signal on
		 * the first pass, so this is ok
		 */
		free( commit_state );

		debug("Supervisor waiting for mirror thread" );
		pthread_join( mirror->thread, NULL );

		success = MS_DONE == mirror_get_state( mirror );

		if( success ){ 
			info( "Mirror supervisor success, exiting" ); }
		else if ( mirror->signal_abandon ) {
			info( "Mirror abandoned" );
			should_retry = 0;
		}
		else if (should_retry){
			info( "Mirror failed, retrying" );
		}
		else { info( "Mirror failed before commit, giving up" ); }

		first_pass = 0;

		if ( should_retry ) { 
			/* We don't want to hammer the destination too
			 * hard, so if this is a retry, insert a delay. */
			sleep( MS_RETRY_DELAY_SECS );

			/* We also have to reset the bitmap to be sure
			 * we transfer everything */
			mirror_reset( mirror );
		}

	} 
	while ( should_retry && !success );

	serve->mirror = NULL;
	serve->mirror_super = NULL;

	mirror_super_destroy( super );
	debug( "Mirror supervisor done." );

	return NULL;
}


#define write_socket(msg) write(client_fd, (msg "\n"), strlen((msg))+1)

/* Call this in the thread where you want to receive the mirror state */
enum mirror_state mirror_super_wait( struct mirror_super * super )
{
	NULLCHECK( super );
	NULLCHECK( super->state_mbox );

	struct mbox * mbox = super->state_mbox;
	enum mirror_state mirror_state;
	enum mirror_state * contents;
	
	contents = (enum mirror_state*)mbox_receive( mbox );
	NULLCHECK( contents );
	
	mirror_state = *contents;
	
	free(contents);
	
	return mirror_state;
}

