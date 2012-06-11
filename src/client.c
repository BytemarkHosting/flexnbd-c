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
	c->socket = socket;
	c->serve = serve;

	c->stop_signal = self_pipe_create();

	return c;
}


void client_signal_stop( struct client *client )
{
	NULLCHECK( client );

	self_pipe_signal( client->stop_signal );
}

void client_destroy( struct client *client )
{
	NULLCHECK( client );

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
				
				if (here)
					fprintf(stderr, ">");
				fprintf(stderr, bitset_is_set_at(map, i) ? "1" : "0");
				if (here)
					fprintf(stderr, "<");
			}
			fprintf(stderr, "\n");
		}
		
		#define DO_READ(dst, len) FATAL_IF_NEGATIVE( \
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


/* Returns 1 if *request was filled with a valid request which we should
 * try to honour. 0 otherwise. */
int client_read_request( struct client * client , struct nbd_request *out_request )
{
	NULLCHECK( client );
	NULLCHECK( out_request );

	struct nbd_request_raw request_raw;
	fd_set                 fds;
	
	FD_ZERO(&fds);
	FD_SET(client->socket, &fds);
	self_pipe_fd_set( client->stop_signal, &fds );
	FATAL_IF_NEGATIVE(select(FD_SETSIZE, &fds, NULL, NULL, NULL), 
	  "select() failed");
	
	if ( self_pipe_fd_isset( client->stop_signal, &fds ) ){
		return 0;
	}

	if (readloop(client->socket, &request_raw, sizeof(request_raw)) == -1) {
		if (errno == 0) {
			debug("EOF reading request");
			return 0; /* neat point to close the socket */
		}
		else {
			FATAL_IF_NEGATIVE(-1, "Error reading request");
		}
	}

	nbd_r2h_request( &request_raw, out_request );

	return 1;
}


/* Writes a reply to request *request, with error, to the client's
 * socket.
 * Returns 1; we don't check for errors on the write.
 * TODO: Check for errors on the write.
 */
int client_write_reply( struct client * client, struct nbd_request *request, int error )
{
	struct nbd_reply     reply;
	struct nbd_reply_raw reply_raw;

	reply.magic = REPLY_MAGIC;
	reply.error = error;
	memcpy( reply.handle, &request->handle, 8 );

	nbd_h2r_reply( &reply, &reply_raw );

	write( client->socket, &reply_raw, sizeof( reply_raw ) );

	return 1;
}

void client_write_init( struct client * client, uint64_t size )
{
	struct nbd_init init;
	struct nbd_init_raw init_raw;

	memcpy( init.passwd, INIT_PASSWD, sizeof( INIT_PASSWD ) );
	init.magic = INIT_MAGIC;
	init.size = size;
	memset( init.reserved, 0, 128 );

	nbd_h2r_init( &init, &init_raw );

	FATAL_IF_NEGATIVE(
		writeloop(client->socket, &init_raw, sizeof(init_raw)),
		"Couldn't send hello"
	);
}

/* Check to see if the client's request needs a reply constructing.
 * Returns 1 if we do, 0 otherwise.
 * request_err is set to 0 if the client sent a bad request, in which
 * case we send an error reply.
 */
int client_request_needs_reply( struct client * client, struct nbd_request request, int *request_err )
{
	debug("request type %d", request.type);
	
	if (request.magic != REQUEST_MAGIC)
		fatal("Bad magic %08x", request.magic);
		
	switch (request.type)
	{
	case REQUEST_READ:
		break;
	case REQUEST_WRITE:
		/* check it's not out of range */
		if ( request.from+request.len > client->serve->size) {
			debug("request read %ld+%d out of range", 
			  request.from, 
			  request.len
			);
			client_write_reply( client, &request, 1 );
			*request_err = 0;
			return 0;
		}
		break;
		
	case REQUEST_DISCONNECT:
		debug("request disconnect");
		*request_err  = 1;
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
	}
}


/* Returns 0 if a request was successfully served. */
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
	info("client cleanup");
	
	if (client->socket)
		close(client->socket);
	if (client->mapped)
		munmap(client->mapped, client->serve->size);
	if (client->fileno)
		close(client->fileno);
}

void* client_serve(void* client_uncast)
{
	struct client* client = (struct client*) client_uncast;
	
	error_set_handler((cleanup_handler*) client_cleanup, client);
	
	//client_open_file(client);
	FATAL_IF_NEGATIVE(
		open_and_mmap(
			client->serve->filename,
			&client->fileno,
			NULL, 
			(void**) &client->mapped
		),
		"Couldn't open/mmap file %s", client->serve->filename
	);
	client_send_hello(client);
	
	while (client_serve_request(client) == 0)
		;
		
	FATAL_IF_NEGATIVE(
		close(client->socket),
		"Couldn't close socket %d", 
		client->socket
	);
	
	client_cleanup(client, 0);
	
	return NULL;
}
