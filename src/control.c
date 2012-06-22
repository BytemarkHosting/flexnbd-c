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

/** The control server responds on a UNIX socket and services our "remote"
 *  commands which are used for changing the access control list, initiating
 *  a mirror process, or asking for status.  The protocol is pretty simple -
 *  after connecting the client sends a series of LF-terminated lines, followed
 *  by a blank line (i.e. double LF).  The first line is taken to be the command
 *  name to invoke, and the lines before the double LF are its arguments.
 *
 *  These commands can be invoked remotely from the command line, with the 
 *  client code to be found in remote.c
 */

#include "control.h"
#include "serve.h"
#include "util.h"
#include "ioutil.h"
#include "parse.h"
#include "readwrite.h"
#include "bitset.h"
#include "self_pipe.h"
#include "acl.h"
#include "status.h"

#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <unistd.h>

struct mirror_status * mirror_status_alloc(
		union mysockaddr * connect_to,
		union mysockaddr * connect_from,
		int max_Bps,
		int action_at_finish,
		struct self_pipe * commit_signal,
		enum mirror_state * out_commit_state)
{
	struct mirror_status * mirror;

	mirror = xmalloc(sizeof(struct mirror_status));
	mirror->connect_to = connect_to;
	mirror->connect_from = connect_from;
	mirror->max_bytes_per_second = max_Bps;
	mirror->action_at_finish = action_at_finish;
	mirror->commit_signal = commit_signal;
	mirror->commit_state = out_commit_state;

	return mirror;
}

void mirror_set_state_f( struct mirror_status * mirror, enum mirror_state state )
{
	NULLCHECK( mirror );
	if ( mirror->commit_state ){
		*mirror->commit_state = state;
	}
}

#define mirror_set_state( mirror, state ) do{\
	debug( "Mirror state => " #state );\
	mirror_set_state_f( mirror, state );\
} while(0)

enum mirror_state mirror_get_state( struct mirror_status * mirror )
{
	NULLCHECK( mirror );
	if ( mirror->commit_state ){
		return *mirror->commit_state;
	} else {
		return MS_UNKNOWN;
	}
}


void mirror_status_init( struct mirror_status * mirror, char * filename )
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
void mirror_status_reset( struct mirror_status * mirror )
{
	NULLCHECK( mirror );
	NULLCHECK( mirror->dirty_map );
	mirror_set_state( mirror, MS_INIT );
	bitset_set(mirror->dirty_map);
}


struct mirror_status * mirror_status_create(
		struct server * serve,
		union mysockaddr * connect_to,
		union mysockaddr * connect_from,
		int max_Bps,
		int action_at_finish,
		struct self_pipe * commit_signal,
		enum mirror_state * out_commit_state)
{
	/* FIXME: shouldn't map_fd get closed? */
	struct mirror_status * mirror;

	NULLCHECK( serve );

	mirror = mirror_status_alloc( connect_to,
			connect_from,
			max_Bps,
			action_at_finish,
			commit_signal,
			out_commit_state );
	
	mirror_status_init( mirror, serve->filename );
	mirror_status_reset( mirror );


	return mirror;
}


void mirror_status_destroy( struct mirror_status *mirror )
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
				/** FIXME: do something useful with bytes/second */

				/** FIXME: error handling code here won't unlock */
				socket_nbd_write( serve->mirror->client, 
						current,
						run,
						0,
						serve->mirror->mapped + current);

				/* now mark it clean */
				bitset_clear_range(map, current, run);
			}
			if (should_lock) { server_unlock_io( serve ); }

			*written += run;
		}
		current += run;

		if (serve->mirror->signal_abandon) {
			success = 0;
			break;
		}
	}

	return success;
}


void mirror_give_control( struct mirror_status * mirror )
{
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


void mirror_cleanup( struct mirror_status * mirror,
		int fatal __attribute__((unused)))
{
	NULLCHECK( mirror );
	info( "Cleaning up mirror thread");

	if( mirror->client && mirror->client > 0 ){
		close( mirror->client );
	}
	mirror->client = -1;
}



int mirror_status_connect( struct mirror_status * mirror, off64_t local_size )
{
	struct sockaddr * connect_from = NULL;
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
	}
	else {
		warn( "Mirror failed to connect.");
		mirror_set_state( mirror, MS_FAIL_CONNECT );
	}

	return mirror_get_state(mirror) == MS_GO;
}



void server_run_mirror( struct server *serve )
{
	NULLCHECK( serve );
	NULLCHECK( serve->mirror );

	int pass;
	uint64_t written;

	info("Starting mirror" );
	for (pass=0; pass < mirror_maximum_passes-1; pass++) {

		debug("mirror start pass=%d", pass);
		if ( !mirror_pass( serve, 1, &written ) ){ return; }
		
		/* if we've not written anything */
		if (written < mirror_last_pass_after_bytes_written) { break; }
	}

	mirror_set_state( serve->mirror, MS_FINALISE );
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


void mirror_signal_commit( struct mirror_status * mirror )
{
	NULLCHECK( mirror );

	self_pipe_signal( mirror->commit_signal );
}

/** Thread launched to drive mirror process */
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
	struct mirror_status * mirror = serve->mirror;
	NULLCHECK( mirror->dirty_map );

	error_set_handler( (cleanup_handler *) mirror_cleanup, mirror );

	info( "Connecting to mirror" );
	
	time_t start_time = time(NULL);
	int connected = mirror_status_connect( mirror, serve->size );
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

	server_run_mirror( serve );

	mirror_set_state( mirror, MS_DONE );
abandon_mirror:
	return NULL;
}


struct mirror_super * mirror_super_create(
		struct server * serve,
		union mysockaddr * connect_to,
		union mysockaddr * connect_from,
		int max_Bps,
		int action_at_finish,
		enum mirror_state * out_commit_state)
{
	struct mirror_super * super = xmalloc( sizeof( struct mirror_super) );
	super->mirror = mirror_status_create( serve, 
			connect_to, 
			connect_from, 
			max_Bps, 
			action_at_finish, 
			self_pipe_create(), 
			out_commit_state );
	super->commit_signal = self_pipe_create();

	return super;
}

void mirror_super_signal_committed( struct mirror_super * super )
{
	NULLCHECK( super );
	self_pipe_signal( super->commit_signal );
}


void mirror_super_destroy( struct mirror_super * super )
{
	NULLCHECK( super );

	mirror_status_destroy( super->mirror );
	self_pipe_destroy( super->commit_signal );
}


/* The mirror supervisor thread.  Responsible for kicking off retries if
 * the mirror thread fails.
 * The mirror_status and mirror_super objects are never freed, and the
 * mirror_super_runner thread is never joined.
 */
void * mirror_super_runner( void * serve_uncast )
{
	struct server * serve = (struct server *) serve_uncast;
	NULLCHECK( serve );
	NULLCHECK( serve->mirror );
	NULLCHECK( serve->mirror_super );

	int should_retry = 0;
	int success = 0;
	fd_set fds;
	int fd_count;

	struct mirror_status * mirror = serve->mirror;
	struct mirror_super *  super  = serve->mirror_super;

	do {
		if ( should_retry ) { 
			/* We don't want to hammer the destination too
			 * hard, so if this is a retry, insert a delay. */
			sleep( MS_RETRY_DELAY_SECS );

			/* We also have to reset the bitmap to be sure
			 * we transfer everything */
			mirror_status_reset( mirror );
		}

		FATAL_IF( 0 != pthread_create(
					&mirror->thread, 
					NULL, 
					mirror_runner, 
					serve),
				"Failed to create mirror thread");

		debug("Supervisor waiting for commit signal");
		FD_ZERO( &fds );
		self_pipe_fd_set( mirror->commit_signal, &fds );
		/* There's no timeout on this select. This means that
		 * the mirror thread *must* signal then abort itself if
		 * it passes the timeout, and it *must* always signal,
		 * no matter what.
		 */
		fd_count = select( FD_SETSIZE, &fds, NULL, NULL, NULL );
		if ( 1 == fd_count ) {
			debug( "Supervisor got commit signal" );
			if ( 0 == should_retry ) {
				should_retry = 1;
				/* Only send this signal the first time */
				mirror_super_signal_committed(super);
				debug("Mirror supervisor committed");
			}
		}
		else { fatal( "Select failed." ); }

		debug("Supervisor waiting for mirror thread" );
		pthread_join( mirror->thread, NULL );
		debug( "Clearing the commit signal. If this blocks,"
				" it's fatal but we can't check in advance." );
		self_pipe_signal_clear( mirror->commit_signal );
		debug( "Commit signal cleared." );

		success = MS_DONE == mirror_get_state( mirror );

		if( success ){ info( "Mirror supervisor success, exiting" ); }
		else if (should_retry){
			warn( "Mirror failed, retrying" );
		}
		else { warn( "Mirror failed before commit, giving up" ); }
	} 
	while ( should_retry && !success );

	serve->mirror = NULL;
	serve->mirror_super = NULL;

	mirror_super_destroy( super );
	debug( "Mirror supervisor done." );

	return NULL;
}


#define write_socket(msg) write(client->socket, (msg "\n"), strlen((msg))+1)

/* We have to pass the mirror_state pointer and the commit_signal
 * separately from the mirror itself because the mirror might have been
 * freed by the time we get to check it */
void mirror_watch_startup( struct control_params * client,
		struct self_pipe * commit_signal,
		enum mirror_state *mirror_state )
{
	NULLCHECK( client );
	struct server * serve = client->serve;
	NULLCHECK( serve );
	struct mirror_status * mirror = serve->mirror;
	NULLCHECK( mirror );

	fd_set fds;
	/* This gives a 61 second timeout for the mirror thread to
	 * either fail or succeed to connect.
	 */
	struct timeval tv = {MS_CONNECT_TIME_SECS+1,0};
	FD_ZERO( &fds );
	self_pipe_fd_set( commit_signal, &fds );
	ERROR_IF_NEGATIVE( select( FD_SETSIZE, &fds, NULL, NULL, &tv ), "Select failed.");

	if ( self_pipe_fd_isset( commit_signal, &fds ) ){
		switch (*mirror_state) {
			case MS_INIT:
			case MS_UNKNOWN:
				write_socket( "1: Mirror failed to initialise" );
				fatal( "Impossible mirror state: %d", *mirror_state );
			case MS_FAIL_CONNECT:
				write_socket( "1: Mirror failed to connect");
				break;
			case MS_FAIL_REJECTED:
				write_socket( "1: Mirror was rejected" );
				break;
			case MS_FAIL_NO_HELLO:
				write_socket( "1: Remote server failed to respond");
				break;
			case MS_FAIL_SIZE_MISMATCH:
				write_socket( "1: Remote size does not match local size" );
				break;
			case MS_GO:
			case MS_FINALISE:
			case MS_DONE: /* Yes, I know we know better, but it's simpler this way */
				write_socket( "0: Mirror started" );
				break;
			default:
				fatal( "Unhandled mirror state: %d", *mirror_state );
		}
	} 
	else {
		/* Timeout.  Badness.  This "should never happen". */
		write_socket( "1: Mirror timed out connecting to remote host" );
	}
}

/** Command parser to start mirror process from socket input */
int control_mirror(struct control_params* client, int linesc, char** lines)
{
	NULLCHECK( client );

	struct server * serve = client->serve;
	union mysockaddr *connect_to = xmalloc( sizeof( union mysockaddr ) );
	union mysockaddr *connect_from = NULL;
	int use_connect_from = 0;
	uint64_t max_Bps = 0;
	int action_at_finish;
	int raw_port;
	
	
	if (linesc < 2) {
		write_socket("1: mirror takes at least two parameters");
		return -1;
	}

	if (parse_ip_to_sockaddr(&connect_to->generic, lines[0]) == 0) {
		write_socket("1: bad IP address");
		return -1;
	}
	
	raw_port = atoi(lines[1]);
	if (raw_port < 0 || raw_port > 65535) {
		write_socket("1: bad IP port number");
		return -1;
	}
	connect_to->v4.sin_port = htobe16(raw_port);

	if (linesc > 2) {
		connect_from = xmalloc( sizeof( union mysockaddr ) );
		if (parse_ip_to_sockaddr(&connect_from->generic, lines[2]) == 0) {
			write_socket("1: bad bind address");
			return -1;
		}
		use_connect_from = 1;
	}

	if (linesc > 3) { max_Bps = atoi(lines[2]); }
	
	action_at_finish = ACTION_EXIT;
	if (linesc > 4) {
		if (strcmp("exit", lines[3]) == 0) {
			action_at_finish = ACTION_EXIT;
		}
		else if (strcmp("nothing", lines[3]) == 0) {
			action_at_finish = ACTION_NOTHING;
		}
		else {
			write_socket("1: action must be 'exit' or 'nothing'");
			return -1;
		}
	}
	
	if (linesc > 5) {
		write_socket("1: unrecognised parameters to mirror");
		return -1;
	}

	enum mirror_state mirror_state;
	serve->mirror_super = mirror_super_create( serve,
			connect_to,
			connect_from,
			max_Bps ,
			action_at_finish,
			&mirror_state );
	serve->mirror = serve->mirror_super->mirror;
	
	FATAL_IF( /* FIXME should free mirror on error */
		0 != pthread_create(
			&serve->mirror_super->thread, 
			NULL, 
			mirror_super_runner, 
			serve
		),
		"Failed to create mirror thread"
	);
	
	mirror_watch_startup( client, serve->mirror_super->commit_signal, &mirror_state );
	debug( "Control thread going away." );
	
	return 0;
}

/** Command parser to alter access control list from socket input */
int control_acl(struct control_params* client, int linesc, char** lines)
{
	NULLCHECK( client );

	struct acl * old_acl = client->serve->acl;
	struct acl * new_acl = acl_create( linesc, lines, old_acl ? old_acl->default_deny : 0 );
	
	if (new_acl->len != linesc) {
		write(client->socket, "1: bad spec: ", 13);
		write(client->socket, lines[new_acl->len], 
		  strlen(lines[new_acl->len]));
		write(client->socket, "\n", 1);
		acl_destroy( new_acl );
	}
	else {
		server_replace_acl( client->serve, new_acl );
		write_socket("0: updated");
	}
	
	return 0;
}

/** FIXME: add some useful statistics */
int control_status(
		struct control_params* client, 
		int linesc __attribute__ ((unused)), 
		char** lines __attribute__((unused))
		)
{
	NULLCHECK( client );
	NULLCHECK( client->serve );
	struct status * status = status_create( client->serve );

	write( client->socket, "0: ", 3 );
	status_write( status, client->socket );
	status_destroy( status );

	return 0;
}

void control_cleanup(struct control_params* client, 
		int fatal __attribute__ ((unused)) )
{
	if (client->socket) { close(client->socket); }
	free(client);
}

/** Master command parser for control socket connections, delegates quickly */
void* control_serve(void* client_uncast)
{
	struct control_params* client = (struct control_params*) client_uncast;
	char **lines = NULL;
	int finished=0;
	
	error_set_handler((cleanup_handler*) control_cleanup, client);
		
	while (!finished) {
		int i, linesc;		
		linesc = read_lines_until_blankline(client->socket, 256, &lines);
		
		if (linesc < 1)
		{
			write(client->socket, "9: missing command\n", 19);
			finished = 1;
			/* ignore failure */
		}
		else if (strcmp(lines[0], "acl") == 0) {
			info("acl command received" );
			if (control_acl(client, linesc-1, lines+1) < 0) {
				finished = 1;
			}
		}
		else if (strcmp(lines[0], "mirror") == 0) {
			info("mirror command received" );
			if (control_mirror(client, linesc-1, lines+1) < 0) {
				finished = 1;
			}
		}
		else if (strcmp(lines[0], "status") == 0) {
			info("status command received" );
			if (control_status(client, linesc-1, lines+1) < 0) {
				finished = 1;
			}
		}
		else {
			write(client->socket, "10: unknown command\n", 23);
			finished = 1;
		}
		
		for (i=0; i<linesc; i++) {
			free(lines[i]);
		}
		free(lines);
	}
	
	control_cleanup(client, 0);
	debug("control command handled" );
	
	return NULL;
}

void accept_control_connection(struct server* params, int client_fd, 
		union mysockaddr* client_address __attribute__ ((unused)) )
{
	pthread_t control_thread;
	struct control_params* control_params;
	
	control_params = xmalloc(sizeof(struct control_params));
	control_params->socket = client_fd;
	control_params->serve = params;

	FATAL_IF(
		0 != pthread_create(
			&control_thread, 
			NULL, 
			control_serve, 
			control_params
		),
		"Failed to create client thread"
	);

	/* FIXME: This thread *really* shouldn't detach
	 * Since it can see the server object, if listen switches mode
	 * while this is live, Bad Things Could Happen.
	 */
	pthread_detach( control_thread );
}

void serve_open_control_socket(struct server* params)
{
	struct sockaddr_un bind_address;
	
	if (!params->control_socket_name) { return; }

	params->control_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	FATAL_IF_NEGATIVE(params->control_fd ,
	  "Couldn't create control socket");
	
	memset(&bind_address, 0, sizeof(bind_address));
	bind_address.sun_family = AF_UNIX;
	strncpy(bind_address.sun_path, params->control_socket_name, sizeof(bind_address.sun_path)-1);
	
	unlink(params->control_socket_name); /* ignore failure */
	
	FATAL_IF_NEGATIVE(
		bind(params->control_fd , &bind_address, sizeof(bind_address)),
		"Couldn't bind control socket to %s",
		params->control_socket_name
	);
	
	FATAL_IF_NEGATIVE(
		listen(params->control_fd , 5),
		"Couldn't listen on control socket"
	);
}

