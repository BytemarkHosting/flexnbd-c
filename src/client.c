#include "client.h"
#include "serve.h"
#include "util.h"
#include "ioutil.h"
#include "bitset.h"
#include "nbdtypes.h"
#include "self_pipe.h"


#include <sys/mman.h>
#include <errno.h>
#include <stdlib.h>


struct client *client_create( struct server *serve, int socket )
{
	NULLCHECK( serve );

	struct client *c;

	c = xmalloc( sizeof( struct server ) );
	c->stopped = 0;
	c->socket = socket;
	c->serve = serve;

	c->stop_signal = self_pipe_create();

	c->entrusted = 0;

	debug( "Alloced client %p (%d, %d)", c, c->stop_signal->read_fd, c->stop_signal->write_fd );
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

	debug( "Destroying stop signal for client %p", client );
	self_pipe_destroy( client->stop_signal );
	free( client );
}



/**
 * So waiting on client->socket is len bytes of data, and we must write it all
 * to client->mapped.  However while doing do we must consult the bitmap
 * client->block_allocation_map, which is a bitmap where one bit represents
 * block_allocation_resolution bytes.  Where a bit isn't set, there are no 
 * disc blocks allocated for that portion of the file, and we'd like to keep
 * it that way.  
 *
 * If the bitmap shows that every block in our prospective write is already
 * allocated, we can proceed as normal and make one call to writeloop.  
 * 
 */
void write_not_zeroes(struct client* client, uint64_t from, int len)
{
	NULLCHECK( client );

	struct bitset_mapping *map = client->serve->allocation_map;

	while (len > 0) {
		/* so we have to calculate how much of our input to consider
		 * next based on the bitmap of allocated blocks.  This will be
		 * at a coarser resolution than the actual write, which may
		 * not fall on a block boundary at either end.  So we look up
		 * how many blocks our write covers, then cut off the start
		 * and end to get the exact number of bytes.
		 */
		 
		int run = bitset_run_count(map, from, len);
		
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
				int blockrun = block_allocation_resolution - 
				  (from % block_allocation_resolution);
				if (blockrun > run)
					blockrun = run;
				
				DO_READ(zerobuffer, blockrun);
				
				/* This reads the buffer twice in the worst case 
				 * but we're leaning on memcmp failing early
				 * and memcpy being fast, rather than try to
				 * hand-optimized something specific.
				 */
				if (zerobuffer[0] != 0 || 
				    memcmp(zerobuffer, zerobuffer + 1, blockrun - 1)) {
					debug("non-zero, writing from=%ld, blockrun=%d", from, blockrun);
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
				else {
					debug("all zero, skip write");
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
int client_read_request( struct client * client , struct nbd_request *out_request )
{
	NULLCHECK( client );
	NULLCHECK( out_request );

	struct nbd_request_raw request_raw;
	fd_set                 fds;
	struct timeval         tv = {CLIENT_MAX_WAIT_SECS, 0};
	struct timeval *       ptv;
	int                    fd_count;

	/* We want a timeout if this is an inbound migration, but not
	 * otherwise
	 */
	ptv = server_is_in_control( client->serve ) ? NULL : &tv;
	
	FD_ZERO(&fds);
	FD_SET(client->socket, &fds);
	self_pipe_fd_set( client->stop_signal, &fds );
	fd_count = select(FD_SETSIZE, &fds, NULL, NULL, ptv);
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

	if( -1 == write( fd, &reply_raw, sizeof( reply_raw ) ) ) {
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



/* Check to see if the client's request needs a reply constructing.
 * Returns 1 if we do, 0 otherwise.
 * request_err is set to 0 if the client sent a bad request, in which
 * case we drop the connection.
 * FIXME: after an ENTRUST, there's no way to distinguish between a
 * DISCONNECT and any bad request.
 */
int client_request_needs_reply( struct client * client, 
		struct nbd_request request,
		int *should_disconnect )
{
	debug("request type %d", request.type);
	
	if (request.magic != REQUEST_MAGIC) {
		fatal("Bad magic %08x", request.magic);
	}
		
	switch (request.type)
	{
	case REQUEST_READ:
		ERROR_IF( client->entrusted,
				"Received a read request "
				"after an entrust message.");
		break;
	case REQUEST_WRITE:
		ERROR_IF( client->entrusted,
				"Received a write request "
				"after an entrust message.");
		/* check it's not out of range */
		if ( request.from+request.len > client->serve->size) {
			debug("request read %ld+%d out of range", 
			  request.from, 
			  request.len
			);
			client_write_reply( client, &request, 1 );
			*should_disconnect = 0;
			return 0;
		}
		break;
		
	case REQUEST_ENTRUST:
		/* Yes, we need to reply to an entrust, but we take no
		 * further action */
		debug("request entrust");
		break;
	case REQUEST_DISCONNECT:
		debug("request disconnect");
		*should_disconnect = 1;
		return 0;
		
	default:
		fatal("Unknown request %08x", request.type);
	}
	return 1;
}


void client_reply_to_entrust( struct client * client, struct nbd_request request )
{
	/* An entrust needs a response, but has no data. */
	debug( "request entrust" );

	client_write_reply( client, &request, 0 );
	/* We set this after trying to send the reply, so we know the
	 * reply got away safely.
	 */
	client->entrusted = 1;
}


void client_reply_to_read( struct client* client, struct nbd_request request )
{
	off64_t offset;

	debug("request read %ld+%d", request.from, request.len);
	client_write_reply( client, &request, 0);

	offset = request.from;
	FATAL_IF_NEGATIVE(
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
	if (client->serve->allocation_map) {
		write_not_zeroes( client, request.from, request.len );
	}
	else {
		FATAL_IF_NEGATIVE(
				readloop(
					client->socket,
					client->mapped + request.from,
					request.len),
				"read failed from=%ld, len=%d",
				request.from,
				request.len );
		server_dirty(client->serve, request.from, request.len);
	}
	
	if (1) /* not sure whether this is necessary... */
	{
		/* multiple of 4K page size */
		uint64_t from_rounded = request.from & (!0xfff);
		uint64_t len_rounded = request.len + (request.from - from_rounded);
		
		FATAL_IF_NEGATIVE(
			msync(
				client->mapped + from_rounded, 
				len_rounded, 
				MS_SYNC),
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
	case REQUEST_ENTRUST:
		client_reply_to_entrust( client, request );
		break;
	}
}


/* Returns 0 if we should continue trying to serve requests */
int client_serve_request(struct client* client)
{
	struct nbd_request    request;
	int                   request_err;
	int                   failure = 1;

	if ( !client_read_request( client, &request ) ) { return failure; }
	if ( !client_request_needs_reply( client, request, &request_err ) )  {
		return request_err;
	} 

	server_lock_io( client->serve );
	{
		if ( !server_is_closed( client->serve ) ) {
			client_reply( client, request );
			failure = 0;
		}
	}
	server_unlock_io( client->serve );

	return failure;
}


void client_send_hello(struct client* client)
{
	client_write_init( client, client->serve->size );
}

void client_cleanup(struct client* client, 
		int fatal __attribute__ ((unused)) )
{
	info("client cleanup for client %p", client);
	
	if (client->socket) { close(client->socket); }
	if (client->mapped) {
		munmap(client->mapped, client->serve->size);
	}
	if (client->fileno) { close(client->fileno); }

	if ( server_io_locked( client->serve ) ) { server_unlock_io( client->serve ); }
	if ( server_acl_locked( client->serve ) ) { server_unlock_acl( client->serve ); }

}

void* client_serve(void* client_uncast)
{
	struct client* client = (struct client*) client_uncast;
	
	error_set_handler((cleanup_handler*) client_cleanup, client);
	
	debug("client: mmap");
	FATAL_IF_NEGATIVE(
		open_and_mmap(
			client->serve->filename,
			&client->fileno,
			NULL, 
			(void**) &client->mapped
		),
		"Couldn't open/mmap file %s", client->serve->filename
	);
	debug("client: sending hello");
	client_send_hello(client);
	
	debug("client: serving requests");
	while (client_serve_request(client) == 0)
		;
	debug("client: stopped serving requests");
	client->stopped = 1;
		
	if ( client->entrusted ){
		debug("client: control arrived" );
		server_control_arrived( client->serve );
	}

	FATAL_IF_NEGATIVE(
		close(client->socket),
		"Couldn't close socket %d", 
		client->socket
	);
	
	debug("Cleaning client %p up normally in thread %p", client, pthread_self());
	client_cleanup(client, 0);
	debug("Client thread done" );
	
	return NULL;
}
