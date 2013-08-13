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
#include "sockutil.h"
#include "parse.h"
#include "readwrite.h"
#include "bitset.h"
#include "self_pipe.h"
#include "status.h"

#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/mman.h>
#include <ev.h>

/* compat with older libev */
#ifndef EVBREAK_ONE

#define ev_run( loop, flags ) ev_loop( loop, flags )

#define ev_break(loop, how) ev_unloop( loop, how )

#define EVBREAK_ONE EVUNLOOP_ONE
#define EVBREAK_ALL EVUNLOOP_ALL

#endif

/* We use this to keep track of the socket request data we need to send */
struct xfer {
	/* Store the bytes we need to send before the data, or receive back */
	union {
		struct nbd_request_raw req_raw;
		struct nbd_reply_raw rsp_raw;
	} hdr;

	/* what in mirror->mapped we should write, and how much of it we've done */
	uint64_t from;
	uint64_t len;
	uint64_t written;

	/* number of bytes of response read */
	uint64_t read;


};

struct mirror_ctrl {
	struct server *serve;
	struct mirror *mirror;

	/* libev stuff */
	struct ev_loop *ev_loop;
	ev_io read_watcher;
	ev_io write_watcher;
	ev_timer timeout_watcher;
	ev_io abandon_watcher;

	/* Use this to keep track of what we're copying at any moment */
	struct xfer xfer;
};

struct mirror * mirror_alloc(
		union mysockaddr * connect_to,
		union mysockaddr * connect_from,
		uint64_t max_Bps,
		enum mirror_finish_action action_at_finish,
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
	mirror->abandon_signal = self_pipe_create();

	if ( mirror->abandon_signal == NULL ) {
		warn( "Couldn't create mirror abandon signal" );
		return NULL;
	}

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

#define mirror_state_is( mirror, state ) mirror_get_state( mirror ) == state


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

	FATAL_IF_NEGATIVE(
		madvise( mirror->mapped, size, MADV_SEQUENTIAL ),
		SHOW_ERRNO( "Failed to madvise() %s", filename )
	);

	mirror->dirty_map = bitset_alloc(size, 4096);
}


/* Call this before a mirror attempt. */
void mirror_reset( struct mirror * mirror )
{
	NULLCHECK( mirror );
	NULLCHECK( mirror->dirty_map );
	mirror_set_state( mirror, MS_INIT );

	/* See the caveats in mirror_run if you change this! */
	bitset_set(mirror->dirty_map);

	mirror->all_dirty = 0;
	mirror->all_clean = 0;
	mirror->pass = 0;
	mirror->this_pass_dirty = 0;
	mirror->this_pass_clean = 0;
	mirror->migration_started = 0;

	return;
}


struct mirror * mirror_create(
		const char * filename,
		union mysockaddr * connect_to,
		union mysockaddr * connect_from,
		uint64_t max_Bps,
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
	self_pipe_destroy( mirror->abandon_signal );
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
#define mirror_last_pass (mirror_maximum_passes - 1)


/* THIS FUNCTION MUST ONLY BE CALLED WITH THE SERVER'S IO LOCKED. */
void mirror_on_exit( struct server * serve )
{
	/* If we're still here, we can shut the server down.
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

	if ( ACTION_UNLINK == serve->mirror->action_at_finish ) {
		debug("Unlinking %s", serve->filename );
		server_unlink( serve );
	}

	debug("Sending disconnect");
	socket_nbd_disconnect( serve->mirror->client );
	info("Mirror sent.");
}


void mirror_cleanup( struct server * serve,
		int fatal __attribute__((unused)))
{
	NULLCHECK( serve );
	struct mirror * mirror = serve->mirror;
	NULLCHECK( mirror );
	info( "Cleaning up mirror thread");

	if ( mirror->mapped ) {
		munmap( mirror->mapped, serve->size );
	}
	mirror->mapped = NULL;

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


int mirror_should_quit( struct mirror * mirror )
{
	switch( mirror->action_at_finish ) {
		case ACTION_EXIT:
		case ACTION_UNLINK:
			return 1;
		default:
			return 0;
	}
}

/* Iterates through the bitmap, finding a dirty run to form the basis of the
 * next transfer, then puts it together. */
int mirror_setup_next_xfer( struct mirror_ctrl *ctrl )
{
	struct mirror* mirror = ctrl->mirror;
	uint64_t current, run, size = ctrl->serve->size;
	int found = 0;

	do {
		int run_is_set = 0;
		current = mirror->this_pass_dirty + mirror->this_pass_clean;

		run = bitset_run_count_ex(
			mirror->dirty_map, current, mirror_longest_write, &run_is_set
		);

		if ( current + run > size ) {
			debug(
				"Size not divisible by %i, adjusting final block",
				block_allocation_resolution
			);
			run = size - current;
		}

		/* FIXME: we could avoid sending sparse areas of the disc here, and
		 * probably save a lot of bandwidth and time (if we know the destination
		 * starts off zeroed). */
		if ( run_is_set ) {
			found = 1;
		} else {
			mirror->this_pass_clean += run;
			mirror->all_clean += run;
		}
	} while ( !found && current + run < size );

	/* current and run specify our next transfer */
	if ( !found ) {
		return 0;
	}
	debug( "Next dirty block: current=%"PRIu64", run=%"PRIu64, current, run );
	struct nbd_request req = {
		.magic = REQUEST_MAGIC,
		.type = REQUEST_WRITE,
		.handle = ".MIRROR.",
		.from = current,
		.len = run
	};
	nbd_h2r_request( &req, &ctrl->xfer.hdr.req_raw );

	ctrl->xfer.from = current;
	ctrl->xfer.len  = run;

	ctrl->xfer.written = 0;
	ctrl->xfer.read = 0;

	return 1;
}

// ONLY CALL THIS WHEN SERVER IO IS LOCKED
void mirror_complete( struct server *serve )
{
	/* FIXME: Pretty sure this is broken, if action != !QUIT. Just moving code
	 * around for now, can fix it later. Action is always quit in production */
	if ( mirror_should_quit( serve->mirror ) ) {
		debug("exit!");
		/* FIXME: This depends on blocking I/O right now, so make sure we are */
		sock_set_nonblock( serve->mirror->client, 0 );
		mirror_on_exit( serve );
		info("Server closed, quitting after successful migration");
	}

	mirror_set_state( serve->mirror, MS_DONE );
	return;
}

static void mirror_write_cb( struct ev_loop *loop, ev_io *w, int revents )
{
	struct mirror_ctrl* ctrl = (struct mirror_ctrl*) w->data;
	NULLCHECK( ctrl );

	struct xfer *xfer = &ctrl->xfer;

	size_t to_write, hdr_size = sizeof( struct nbd_request_raw );
	char *data_loc;
	ssize_t count;

	if ( !( revents & EV_WRITE ) ) {
		warn( "No write event signalled in mirror write callback" );
		return;
	}

	debug( "Mirror write callback invoked with events %d. fd: %i", revents, ctrl->mirror->client );

	if ( xfer->written < hdr_size ) {
		data_loc = ( (char*) &xfer->hdr.req_raw ) + ctrl->xfer.written;
		to_write = hdr_size - xfer->written;
	} else {
		data_loc = ctrl->mirror->mapped  + xfer->from + ( xfer->written - hdr_size );
		to_write = xfer->len - ( ctrl->xfer.written - hdr_size );

		// If we're in the last pass, we'll be locked anyway. If we're not in
		// the last pass, we want to be locked for every write() call that
		// we issue, to avoid the blocks being updated while we work. In
		// particular, bitset_run_clear() must be called while the I/O is locked
		// or we might clear a bit that had been set by another write.
		if ( !server_io_locked( ctrl->serve ) ) {
			server_lock_io( ctrl->serve );
			debug( "In block block" );
		}

	}

	// Actually read some bytes
	if ( ( count = write( ctrl->mirror->client, data_loc, to_write ) ) < 0 ) {
		if ( errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR ) {
			warn( SHOW_ERRNO( "Couldn't write to listener" ) );
			ev_break( loop, EVBREAK_ONE );
		}
		return;
	}
	debug( "Wrote %"PRIu64" bytes", count );
	debug( "to_write was %"PRIu64", xfer->written was %"PRIu64, to_write, xfer->written );
	ctrl->xfer.written += count;

	// We write some bytes, so reset the timer
	ev_timer_again( ctrl->ev_loop, &ctrl->timeout_watcher );

	// All bytes written, so now we need to read the NBD reply back.
	if ( ctrl->xfer.written == ctrl->xfer.len + hdr_size ) {

		// We can, however, clear the run here. If it turns out that the
		// NBD request has been rejected, we're discarding it anyway, so the
		// wrong data won't get used. If the request is a success, any blocks
		// written to while waiting for the reply will be copied in the next
		// pass; if it's the final pass, I/O remains locked.
		debug( "Clearing bitset from=%"PRIu64" run=%"PRIu64", ctr->xfer.from, ctrl->xfer.len" );
		bitset_clear_range( ctrl->mirror->dirty_map, ctrl->xfer.from, ctrl->xfer.len );

		if ( ctrl->mirror->pass != mirror_last_pass ) {
			debug( "Leaving lock block" );
			server_unlock_io( ctrl->serve );
		}
		ev_io_start( loop, &ctrl->read_watcher  );
		ev_io_stop(  loop, &ctrl->write_watcher );
	}

	return;
}

static void mirror_read_cb( struct ev_loop *loop, ev_io *w, int revents )
{
	struct mirror_ctrl* ctrl = (struct mirror_ctrl*) w->data;
	NULLCHECK( ctrl );

	struct mirror *m = ctrl->mirror;
	NULLCHECK( m );

	struct xfer *xfer = &ctrl->xfer;
	NULLCHECK( xfer );

	if ( !( revents & EV_READ ) ) {
		warn( "No read event signalled in mirror read callback" );
		return;
	}

	struct nbd_reply rsp;
	ssize_t count;
	uint64_t left = sizeof( struct nbd_reply_raw ) - xfer->read;

	debug( "Mirror read callback invoked with events %d. fd:%i", revents, m->client );

	/* Start / continue reading the NBD response from the mirror. */
	if ( ( count = read( m->client, ((void*) &xfer->hdr.rsp_raw) + xfer->read, left ) ) < 0 ) {
		if ( errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR ) {
			warn( SHOW_ERRNO( "Couldn't read from listener" ) );
			ev_break( loop, EVBREAK_ONE );
		}
		debug( SHOW_ERRNO( "Couldn't read from listener (non-scary)" ) );
		return;
	}

	info( "count is %li, left was %"PRIu64, count, left );

	if ( count == 0 ) {
		warn( "EOF reading response from server!" );
		ev_break( loop, EVBREAK_ONE );
		return;
	}

	// We read some bytes, so reset the timer
	ev_timer_again( ctrl->ev_loop, &ctrl->timeout_watcher );

	debug( "Read %"PRIu64" bytes", count );
	debug( "left was %"PRIu64", xfer->read was %"PRIu64, left, xfer->read );
	xfer->read += count;

	if ( xfer->read < sizeof( struct nbd_reply_raw ) ) {
		// Haven't read the whole response yet
		return;
	}

	nbd_r2h_reply( &xfer->hdr.rsp_raw, &rsp );

	// validate reply, break event loop if bad
	if ( rsp.magic != REPLY_MAGIC ) {
		warn( "Bad reply magic from listener" );
		ev_break( loop, EVBREAK_ONE );
		return;
	}

	if ( rsp.error != 0 ) {
		warn( "Error returned from listener: %i", rsp.error );
		ev_break( loop, EVBREAK_ONE );
		return;
	}

	if ( memcmp( ".MIRROR.", &rsp.handle[0], 8 ) != 0 ) {
		warn( "Bad handle returned from listener" );
		ev_break( loop, EVBREAK_ONE );
		return;
	}

	/* transfer was completed, so now we need to either set up the next
	 * transfer of this pass, set up the first transfer of the next pass, or
	 * complete the migration */
	m->this_pass_dirty += xfer->len;
	m->all_dirty += xfer->len;
	xfer->read = 0;
	xfer->written = 0;

	/* This next bit could take a little while, which is fine */
	ev_timer_stop( ctrl->ev_loop, &ctrl->timeout_watcher );

	do {
		// This pass complete
		if ( m->this_pass_dirty + m->this_pass_clean == ctrl->serve->size ) {
			debug( "Pass %d completed", m->pass );
			/* Set up the next transfer, which may be n+1 in the current pass,
			 * or 0 in a new pass. If we can't find another transfer to do, that
			 * means the pass is complete. Advance pass and re-run the end-of-
			 * pass logic to complete migration ( pass == mirror_last_pass ), or
			 * move onto the last pass ( pass < mirror_last_pass, by virtue of
			 * this_pass_dirty being 0 ).
			 */

			 // last pass completed
			if ( m->pass >= mirror_last_pass ) {
				/* This was the last pass, so finish. */
				mirror_complete( ctrl->serve );
				ev_break( loop, EVBREAK_ONE );
				return;
			}

			// this was not the last pass - set up for the next run.
			if ( m->this_pass_dirty < mirror_last_pass_after_bytes_written ) {
				/* Quiet disc, so skip to the final pass */
				m->pass = mirror_last_pass;
			} else {
				m->pass++;
			}
			// FIXME: Can status race with us if it inspects state here?
			m->this_pass_dirty = 0;
			m->this_pass_clean = 0;

			debug( "mirror start pass=%d", m->pass );
			/* This is the start of our next pass. If it happens to be the
			 * final pass, we need to lock server I/O so that other writes
			 * don't race with our call to mirror_setup_next_xfer() below */
			if ( m->pass == mirror_last_pass ) {
				debug( "In lock block for last pass" );
				server_lock_io( ctrl->serve );
			}
		}
	}  while ( !mirror_setup_next_xfer( ctrl ) );

	/* We're waiting for the socket to become writable again, so re-enable */
	ev_timer_again( ctrl->ev_loop, &ctrl->timeout_watcher );
	ev_io_start( loop, &ctrl->write_watcher );
	ev_io_stop(  loop, &ctrl->read_watcher );
	return;
}

void mirror_timeout_cb( struct ev_loop *loop, ev_timer *w __attribute__((unused)), int revents )
{
	if ( !(revents & EV_TIMER ) ) {
		warn( "Mirror timeout called but no timer event signalled" );
		return;
	}

	info( "Mirror timeout signalled" );
	ev_break( loop, EVBREAK_ONE );
	return;
}

void mirror_abandon_cb( struct ev_loop *loop, ev_io *w, int revents )
{
	struct mirror_ctrl* ctrl = (struct mirror_ctrl*) w->data;
	NULLCHECK( ctrl );

	if ( !(revents & EV_READ ) ) {
		warn( "Mirror abandon called but no abandon event signalled" );
		return;
	}

	debug( "Abandon message received" );
	mirror_set_state( ctrl->mirror, MS_ABANDONED );
	self_pipe_signal_clear( ctrl->mirror->abandon_signal );
	ev_break( loop, EVBREAK_ONE );
	return;
}

void mirror_run( struct server *serve )
{
	NULLCHECK( serve );
	NULLCHECK( serve->mirror );

	serve->mirror->migration_started = monotonic_time_ms();
	info("Starting mirror" );

	/* mirror_setup_next_xfer won't be able to cope with this, so special-case
	 * it here.
	 * TODO: Another case we won't be able to handle is a non-zero-sized image
	 * where none of the blocks are set in the first pass. As it happens, we
	 * start with all blocks set and then pare them down, so it doesn't happen
	 * in the current codebase - but watch out for the future!
	 */
	if ( serve->size == 0 ) {
		info( "0-byte image special case" );
		server_lock_io( serve );
		mirror_complete( serve );
		server_unlock_io( serve );
		return;
	}

	struct mirror_ctrl ctrl;
	memset( &ctrl, 0, sizeof( struct mirror_ctrl ) );

	ctrl.serve  = serve;
	ctrl.mirror = serve->mirror;

	ctrl.ev_loop = EV_DEFAULT;

	/* gcc warns on -O2. clang is fine. Seems to be the fault of ev.h */
	ev_io_init( &ctrl.read_watcher, mirror_read_cb, ctrl.mirror->client, EV_READ  );
	ctrl.read_watcher.data = (void*) &ctrl;

	ev_io_init( &ctrl.write_watcher, mirror_write_cb, ctrl.mirror->client, EV_WRITE );
	ctrl.write_watcher.data = (void*) &ctrl;

	ev_init( &ctrl.timeout_watcher, mirror_timeout_cb );
	ctrl.timeout_watcher.repeat = MS_REQUEST_LIMIT_SECS_F ;

	ev_init( &ctrl.abandon_watcher, mirror_abandon_cb );
	ev_io_set( &ctrl.abandon_watcher, ctrl.mirror->abandon_signal->read_fd, EV_READ );
	ctrl.abandon_watcher.data = (void*) &ctrl;
	ev_io_start( ctrl.ev_loop, &ctrl.abandon_watcher );

	ERROR_UNLESS(
		mirror_setup_next_xfer( &ctrl ),
		"Couldn't find first transfer for mirror!"
	);

	/* Start by writing xfer 0 to the listener */
	ev_io_start( ctrl.ev_loop, &ctrl.write_watcher );

	/* Everything up to here is blocking. We switch to non-blocking so we
	 * can handle rate-limiting and weird error conditions better. TODO: We
	 * should expand the event loop upwards so we can do the same there too */
	sock_set_nonblock( ctrl.mirror->client, 1 );
	info( "Entering event loop" );
	ev_run( ctrl.ev_loop, 0 );
	info( "Exited event loop" );
	/* Parent code might expect a non-blocking socket */
	sock_set_nonblock( ctrl.mirror->client, 0 );


	/* Errors in the event loop don't track I/O lock state or try to restore
	 * it to something sane - they just terminate the event loop with state !=
	 * MS_DONE. We unlock here if it's locked.
	 */
	if ( server_io_locked( serve ) ) {
		server_unlock_io( serve );
	}

	if ( serve->mirror->commit_state != MS_DONE ) {
		error( "Event loop exited, but mirroring is not complete" );
	}

	/* returning here says "mirroring complete" to the runner */
	return;
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
abandon_mirror:
	return NULL;
}


struct mirror_super * mirror_super_create(
		const char * filename,
		union mysockaddr * connect_to,
		union mysockaddr * connect_from,
		uint64_t max_Bps,
		enum mirror_finish_action action_at_finish,
		struct mbox * state_mbox)
{
	struct mirror_super * super = xmalloc( sizeof( struct mirror_super) );
	super->mirror = mirror_create(
			filename,
			connect_to,
			connect_from,
			max_Bps,
			action_at_finish,
			mbox_create() ) ;
	super->state_mbox = state_mbox;
	return super;
}


/* Post the current state of the mirror into super->state_mbox.*/
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
	int success = 0, abandoned = 0;

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
			/* Only retry if the connection attempt was successful.  Otherwise
			 * the user will see an error reported while we're still trying to
			 * retry behind the scenes. This may race with migration completing
			 * but since we "shouldn't retry" in that case either, that's fine
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

		/* If we can't connect to the remote end, the watcher for the abandon
		 * signal never gets installed at the moment, which is why we also check
		 * it here. */
		abandoned =
		  mirror_get_state( mirror ) == MS_ABANDONED ||
		  self_pipe_signal_clear( mirror->abandon_signal );

		success = MS_DONE == mirror_get_state( mirror );


		if( success ){
			info( "Mirror supervisor success, exiting" );
		} else if ( abandoned ) {
			info( "Mirror abandoned" );
			should_retry = 0;
		} else if ( should_retry ) {
			info( "Mirror failed, retrying" );
		} else {
			info( "Mirror failed before commit, giving up" );
		}

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

	return NULL;
}

