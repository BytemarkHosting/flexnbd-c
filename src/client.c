#include "client.h"
#include "serve.h"
#include "ioutil.h"
#include "sockutil.h"
#include "util.h"
#include "bitset.h"
#include "nbdtypes.h"
#include "self_pipe.h"

#include <sys/mman.h>
#include <errno.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

struct client *client_create( struct server *serve, int socket )
{
	NULLCHECK( serve );

	struct client *c;
	struct sigevent evp = {
		.sigev_notify = SIGEV_SIGNAL,
		.sigev_signo = CLIENT_KILLSWITCH_SIGNAL
	};

	c = xmalloc( sizeof( struct client ) );
	c->stopped = 0;
	c->socket = socket;
	c->serve = serve;

	c->stop_signal = self_pipe_create();

	FATAL_IF_NEGATIVE(
		timer_create( CLOCK_MONOTONIC, &evp, &(c->killswitch) ),
		SHOW_ERRNO( "Failed to create killswitch timer" )
	);

	debug( "Alloced client %p with socket %d", c, socket );
	return c;
}


void client_signal_stop( struct client *c)
{
	NULLCHECK( c);

	debug("client %p: signal stop (%d, %d)", c,c->stop_signal->read_fd, c->stop_signal->write_fd );
	self_pipe_signal( c->stop_signal );
}

void client_destroy( struct client *client )
{
	NULLCHECK( client );

	FATAL_IF_NEGATIVE(
		timer_delete( client->killswitch ),
		SHOW_ERRNO( "Couldn't delete killswitch" )
	);

	debug( "Destroying stop signal for client %p", client );
	self_pipe_destroy( client->stop_signal );
	debug( "Freeing client %p", client );
	free( client );
}



/**
 * So waiting on client->socket is len bytes of data, and we must write it all
 * to client->mapped.  However while doing do we must consult the bitmap
 * client->serve->allocation_map, which is a bitmap where one bit represents
 * block_allocation_resolution bytes.  Where a bit isn't set, there are no
 * disc blocks allocated for that portion of the file, and we'd like to keep
 * it that way.
 *
 * If the bitmap shows that every block in our prospective write is already
 * allocated, we can proceed as normal and make one call to writeloop.
 *
 */
void write_not_zeroes(struct client* client, uint64_t from, uint64_t len)
{
	NULLCHECK( client );
	NULLCHECK( client->serve );
	NULLCHECK( client->serve->allocation_map );

	struct bitset_mapping *map = client->serve->allocation_map;

	while (len > 0) {
		/* so we have to calculate how much of our input to consider
		 * next based on the bitmap of allocated blocks.  This will be
		 * at a coarser resolution than the actual write, which may
		 * not fall on a block boundary at either end.  So we look up
		 * how many blocks our write covers, then cut off the start
		 * and end to get the exact number of bytes.
		 */

		uint64_t run = bitset_run_count(map, from, len);

		debug("write_not_zeroes: from=%ld, len=%d, run=%d", from, len, run);

		if (run > len) {
			run = len;
			debug("(run adjusted to %d)", run);
		}

		if (0) /* useful but expensive */
		{
			uint64_t i;
			fprintf(stderr, "full map resolution=%d: ", map->resolution);
			for (i=0; i<client->serve->size; i+=map->resolution) {
				int here = (from >= i && from < i+map->resolution);

				if (here) { fprintf(stderr, ">"); }
				fprintf(stderr, bitset_is_set_at(map, i) ? "1" : "0");
				if (here) { fprintf(stderr, "<"); }
			}
			fprintf(stderr, "\n");
		}

		#define DO_READ(dst, len) ERROR_IF_NEGATIVE( \
			readloop( \
				client->socket, \
				(dst), \
				(len) \
			), \
			"read failed %ld+%d", from, (len) \
		)

		if (bitset_is_set_at(map, from)) {
			debug("writing the lot: from=%ld, run=%d", from, run);
			/* already allocated, just write it all */
			DO_READ(client->mapped + from, run);
			server_dirty(client->serve, from, run);
			len  -= run;
			from += run;
		}
		else {
			char zerobuffer[block_allocation_resolution];
			/* not allocated, read in block_allocation_resoution */
			while (run > 0) {
				uint64_t blockrun = block_allocation_resolution -
				  (from % block_allocation_resolution);
				if (blockrun > run)
					blockrun = run;

				DO_READ(zerobuffer, blockrun);

				/* This reads the buffer twice in the worst case
				 * but we're leaning on memcmp failing early
				 * and memcpy being fast, rather than try to
				 * hand-optimized something specific.
				 */

				int all_zeros = (zerobuffer[0] == 0) &&
					(0 == memcmp( zerobuffer, zerobuffer+1, blockrun-1 ));

				if ( !all_zeros ) {
					memcpy(client->mapped+from, zerobuffer, blockrun);
					bitset_set_range(map, from, blockrun);
					server_dirty(client->serve, from, blockrun);
					/* at this point we could choose to
					 * short-cut the rest of the write for
					 * faster I/O but by continuing to do it
					 * the slow way we preserve as much
					 * sparseness as possible.
					 */
				}

				len  -= blockrun;
				run  -= blockrun;
				from += blockrun;
			}
		}
	}
}


int fd_read_request( int fd, struct nbd_request_raw *out_request)
{
	return readloop(fd, out_request, sizeof(struct nbd_request_raw));
}


/* Returns 1 if *request was filled with a valid request which we should
 * try to honour. 0 otherwise. */
int client_read_request( struct client * client , struct nbd_request *out_request, int * disconnected )
{
	NULLCHECK( client );
	NULLCHECK( out_request );

	struct nbd_request_raw request_raw;
	fd_set                 fds;
	struct timeval *       ptv = NULL;
	int                    fd_count;

	/* We want a timeout if this is an inbound migration, but not otherwise.
	 * This is compile-time selectable, as it will break mirror max_bps
	 */
#ifdef HAS_LISTEN_TIMEOUT
	struct timeval         tv = {CLIENT_MAX_WAIT_SECS, 0};

	if ( !server_is_in_control( client->serve ) ) {
		ptv = &tv;
	}
#endif

	FD_ZERO(&fds);
	FD_SET(client->socket, &fds);
	self_pipe_fd_set( client->stop_signal, &fds );
	fd_count = sock_try_select(FD_SETSIZE, &fds, NULL, NULL, ptv);
	if ( fd_count == 0 ) {
		/* This "can't ever happen" */
		if ( NULL == ptv ) { fatal( "No FDs selected, and no timeout!" ); }
		else { error("Timed out waiting for I/O"); }
	}
	else if ( fd_count < 0 ) { fatal( "Select failed" ); }

	if ( self_pipe_fd_isset( client->stop_signal, &fds ) ){
		debug("Client received stop signal.");
		return 0;
	}

	if (fd_read_request(client->socket, &request_raw) == -1) {
		*disconnected = 1;
		switch( errno ){
			case 0:
				debug( "EOF while reading request" );
				return 0;
			case ECONNRESET:
				debug( "Connection reset while"
						" reading request" );
				return 0;
			default:
				/* FIXME: I've seen this happen, but I
				 * couldn't reproduce it so I'm leaving
				 * it here with a better debug output in
				 * the hope it'll spontaneously happen
				 * again.  It should *probably* be an
				 * error() call, but I want to be sure.
				 * */
				fatal("Error reading request: %d, %s",
						errno,
						strerror( errno ));
		}
	}

	nbd_r2h_request( &request_raw, out_request );

	return 1;
}

int fd_write_reply( int fd, char *handle, int error )
{
	struct nbd_reply     reply;
	struct nbd_reply_raw reply_raw;

	reply.magic = REPLY_MAGIC;
	reply.error = error;
	memcpy( reply.handle, handle, 8 );

	nbd_h2r_reply( &reply, &reply_raw );
	debug( "Replying with %s, %d", handle, error );

	if( -1 == writeloop( fd, &reply_raw, sizeof( reply_raw ) ) ) {
		switch( errno ) {
			case ECONNRESET:
				error( "Connection reset while writing reply" );
				break;
			case EBADF:
				fatal( "Tried to write to an invalid file descriptor" );
				break;
			case EPIPE:
				error( "Remote end closed" );
				break;
			default:
				fatal( "Unhandled error while writing: %d", errno );
		}
	}

	return 1;
}


/* Writes a reply to request *request, with error, to the client's
 * socket.
 * Returns 1; we don't check for errors on the write.
 * TODO: Check for errors on the write.
 */
int client_write_reply( struct client * client, struct nbd_request *request, int error )
{
	return fd_write_reply( client->socket, request->handle, error);
}


void client_write_init( struct client * client, uint64_t size )
{
	struct nbd_init init = {{0}};
	struct nbd_init_raw init_raw = {{0}};

	memcpy( init.passwd, INIT_PASSWD, sizeof( INIT_PASSWD ) );
	init.magic = INIT_MAGIC;
	init.size = size;
	memset( init.reserved, 0, 128 );

	nbd_h2r_init( &init, &init_raw );

	ERROR_IF_NEGATIVE(
		writeloop(client->socket, &init_raw, sizeof(init_raw)),
		"Couldn't send hello"
	);
}


/* Remove len bytes from the client socket. This is needed when the
 * client sends a write we can't honour - we need to get rid of the
 * bytes they've already written before we can look for another request.
 */
void client_flush( struct client * client, size_t len )
{
	int devnull = open("/dev/null", O_WRONLY);
	FATAL_IF_NEGATIVE( devnull,
			"Couldn't open /dev/null: %s", strerror(errno));
	int pipes[2];
	pipe( pipes );

	const unsigned int flags = SPLICE_F_MORE | SPLICE_F_MOVE;
	size_t spliced = 0;

	while ( spliced < len ) {
		ssize_t received = splice(
				client->socket, NULL,
				pipes[1], NULL,
				len-spliced, flags );
		FATAL_IF_NEGATIVE( received,
				"splice error: %s",
				strerror(errno));
		ssize_t junked = 0;
		while( junked < received ) {
			ssize_t junk;
			junk = splice(
				pipes[0], NULL,
				devnull, NULL,
				received, flags );
			FATAL_IF_NEGATIVE( junk,
				"splice error: %s",
				strerror(errno));
			junked += junk;
		}
		spliced += received;
	}
	debug("Flushed %d bytes", len);


	close( devnull );
}


/* Check to see if the client's request needs a reply constructing.
 * Returns 1 if we do, 0 otherwise.
 * request_err is set to 0 if the client sent a bad request, in which
 * case we drop the connection.
 */
int client_request_needs_reply( struct client * client,
		struct nbd_request request )
{
	/* The client is stupid, but don't take down the whole server as a result.
	 * We send a reply before disconnecting so that at least some indication of
	 * the problem is visible, and so proxies don't retry the same (bad) request
	 * forever.
	 */
	if (request.magic != REQUEST_MAGIC) {
		warn("Bad magic 0x%08x from client", request.magic);
		client_write_reply( client, &request, EBADMSG );
		client->disconnect = 1; // no need to flush
		return 0;
	}

	debug(
		"request type=%"PRIu32", from=%"PRIu64", len=%"PRIu32,
		request.type, request.from, request.len
	);

	/* check it's not out of range */
	if ( request.from+request.len > client->serve->size) {
		warn("write request %"PRIu64"+%"PRIu32" out of range",
		  request.from, request.len
		);
		if ( request.type == REQUEST_WRITE ) {
			client_flush( client, request.len );
		}
		client_write_reply( client, &request, EPERM ); /* TODO: Change to ERANGE ? */
		client->disconnect = 0;
		return 0;
	}


	switch (request.type)
	{
	case REQUEST_READ:
		break;
	case REQUEST_WRITE:
		break;
	case REQUEST_DISCONNECT:
		debug("request disconnect");
		client->disconnect = 1;
		return 0;

	default:
		fatal("Unknown request %08x", request.type);
	}
	return 1;
}


void client_reply_to_read( struct client* client, struct nbd_request request )
{
	off64_t offset;

	debug("request read %ld+%d", request.from, request.len);
	client_write_reply( client, &request, 0);

	offset = request.from;

	/* If we get cut off partway through this sendfile, we don't
	 * want to kill the server.  This should be an error.
	 */
	ERROR_IF_NEGATIVE(
			sendfileloop(
				client->socket,
				client->fileno,
				&offset,
				request.len),
			"sendfile failed from=%ld, len=%d",
			offset,
			request.len);
}


void client_reply_to_write( struct client* client, struct nbd_request request )
{
	debug("request write %ld+%d", request.from, request.len);
	if (client->serve->allocation_map_built) {
		write_not_zeroes( client, request.from, request.len );
	}
	else {
		debug("No allocation map, writing directly.");
		/* If we get cut off partway through reading this data:
		 * */
		ERROR_IF_NEGATIVE(
			readloop( client->socket,
				client->mapped + request.from,
				request.len),
			"reading write data failed from=%ld, len=%d",
			request.from,
			request.len
		);

		/* Ensure this updated block is written in the event of a mirror op */
		server_dirty(client->serve, request.from, request.len);
		/* the allocation_map is shared between client threads, and may be
		 * being built. We need to reflect the write in it, as it may be in
		 * a position the builder has already gone over.
		 */
		bitset_set_range(client->serve->allocation_map, request.from, request.len);
	}

	if (1) /* not sure whether this is necessary... */
	{
		/* multiple of 4K page size */
		uint64_t from_rounded = request.from & (!0xfff);
		uint64_t len_rounded = request.len + (request.from - from_rounded);

		FATAL_IF_NEGATIVE(
			msync( client->mapped + from_rounded,
				len_rounded,
				MS_SYNC | MS_INVALIDATE),
			"msync failed %ld %ld", request.from, request.len
		);
	}
	client_write_reply( client, &request, 0);
}


void client_reply( struct client* client, struct nbd_request request )
{
	switch (request.type) {
	case REQUEST_READ:
		client_reply_to_read( client, request );
		break;
	case REQUEST_WRITE:
		client_reply_to_write( client, request );
		break;
	}
}


/* Starts a timer that will kill the whole process if disarm is not called
 * within a timeout (see CLIENT_HANDLE_TIMEOUT).
 */
void client_arm_killswitch( struct client* client )
{
	struct itimerspec its = {
		.it_value    = { .tv_nsec = 0, .tv_sec = CLIENT_HANDLER_TIMEOUT },
		.it_interval = { .tv_nsec = 0, .tv_sec = 0 }
	};

	if ( !client->serve->use_killswitch ) {
		return;
	}

	debug( "Arming killswitch" );

	FATAL_IF_NEGATIVE(
		timer_settime( client->killswitch, 0, &its, NULL ),
		SHOW_ERRNO( "Failed to arm killswitch" )
	);

	return;
}

void client_disarm_killswitch( struct client* client )
{
	struct itimerspec its = {
		.it_value    = { .tv_nsec = 0, .tv_sec = 0 },
		.it_interval = { .tv_nsec = 0, .tv_sec = 0 }
	};

	if ( !client->serve->use_killswitch ) {
		return;
	}

	debug( "Disarming killswitch" );

	FATAL_IF_NEGATIVE(
		timer_settime( client->killswitch, 0, &its, NULL ),
		SHOW_ERRNO( "Failed to disarm killswitch" )
	);

	return;
}

/* Returns 0 if we should continue trying to serve requests */
int client_serve_request(struct client* client)
{
	struct nbd_request    request = {0};
	int                   stop = 1;
	int                   disconnected = 0;

	if ( !client_read_request( client, &request, &disconnected ) ) { return stop; }
	if ( disconnected ) { return stop; }
	if ( !client_request_needs_reply( client, request ) )  {
		return client->disconnect;
	}

	server_lock_io( client->serve );
	{
		if ( !server_is_closed( client->serve ) ) {
			/* We arm / disarm around client_reply() to catch cases where the
			 * remote peer sends part of a write request data before dying,
			 * and cases where we send part of read reply data before they die.
			 *
			 * That last is theoretical right now, but could break us in the
			 * same way as a half-write (which causes us to sit in read forever)
			 *
			 * We only arm/disarm inside the server io lock because it's common
			 * during migrations for us to be hanging on that mutex for quite
			 * a while while the final pass happens - it's held for the entire
			 * time.
			 */
			client_arm_killswitch( client );
			client_reply( client, request );
			client_disarm_killswitch( client );
			stop = 0;
		}
	}
	server_unlock_io( client->serve );


	return stop;
}


void client_send_hello(struct client* client)
{
	client_write_init( client, client->serve->size );
}

void client_cleanup(struct client* client,
		int fatal __attribute__ ((unused)) )
{
	info("client cleanup for client %p", client);

	if (client->socket) {
		FATAL_IF_NEGATIVE( close(client->socket),
			"Error closing client socket %d",
			client->socket );
		debug("Closed client socket fd %d", client->socket);
		client->socket = -1;
	}
	if (client->mapped) {
		munmap(client->mapped, client->serve->size);
	}
	if (client->fileno) {
		FATAL_IF_NEGATIVE( close(client->fileno),
			"Error closing file %d",
			client->fileno );
		debug("Closed client file fd %d", client->fileno );
		client->fileno = -1;
	}

	if ( server_io_locked( client->serve ) ) { server_unlock_io( client->serve ); }
	if ( server_acl_locked( client->serve ) ) { server_unlock_acl( client->serve ); }

}

void* client_serve(void* client_uncast)
{
	struct client* client = (struct client*) client_uncast;

	error_set_handler((cleanup_handler*) client_cleanup, client);

	info("client: mmaping file");
	FATAL_IF_NEGATIVE(
		open_and_mmap(
			client->serve->filename,
			&client->fileno,
			NULL,
			(void**) &client->mapped
		),
		"Couldn't open/mmap file %s: %s", client->serve->filename, strerror( errno )
	);

	FATAL_IF_NEGATIVE(
		madvise( client->mapped, client->serve->size, MADV_RANDOM ),
		SHOW_ERRNO( "Failed to madvise() %s", client->serve->filename )
	);

	debug( "Opened client file fd %d", client->fileno);
	debug("client: sending hello");
	client_send_hello(client);

	debug("client: serving requests");
	while (client_serve_request(client) == 0)
		;
	debug("client: stopped serving requests");
	client->stopped = 1;

	if ( client->disconnect ){
		debug("client: control arrived" );
		server_control_arrived( client->serve );
	}

	debug("Cleaning client %p up normally in thread %p", client, pthread_self());
	client_cleanup(client, 0);
	debug("Client thread done" );

	return NULL;
}

