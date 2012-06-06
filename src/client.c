#include "client.h"
#include "serve.h"
#include "util.h"
#include "ioutil.h"
#include "bitset.h"
#include "nbdtypes.h"


#include <sys/mman.h>
#include <errno.h>
#include <stdlib.h>


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
void write_not_zeroes(struct client* client, off64_t from, int len)
{
	char *map = client->serve->block_allocation_map;

	while (len > 0) {
		/* so we have to calculate how much of our input to consider
		 * next based on the bitmap of allocated blocks.  This will be
		 * at a coarser resolution than the actual write, which may
		 * not fall on a block boundary at either end.  So we look up
		 * how many blocks our write covers, then cut off the start
		 * and end to get the exact number of bytes.
		 */
		int first_bit = from/block_allocation_resolution;
		int last_bit  = (from+len+block_allocation_resolution-1) / 
		  block_allocation_resolution;
		int run = bit_run_count(map, first_bit, last_bit-first_bit) *
		  block_allocation_resolution;
		
		if (run > len)
			run = len;
		
		debug("write_not_zeroes: %ld+%d, first_bit=%d, last_bit=%d, run=%d", 
		  from, len, first_bit, last_bit, run);
		
		#define DO_READ(dst, len) CLIENT_ERROR_ON_FAILURE( \
			readloop( \
				client->socket, \
				(dst), \
				(len) \
			), \
			"read failed %ld+%d", from, (len) \
		)
		
		if (bit_is_set(map, from/block_allocation_resolution)) {
			debug("writing the lot");
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
				char *dst = client->mapped+from;
				int bit = from/block_allocation_resolution;
				int blockrun = block_allocation_resolution - 
				  (from % block_allocation_resolution);
				if (blockrun > run)
					blockrun = run;
				
				debug("writing partial: bit=%d, blockrun=%d (run=%d)",
				  bit, blockrun, run);
				
				DO_READ(zerobuffer, blockrun);
				
				/* This reads the buffer twice in the worst case 
				 * but we're leaning on memcmp failing early
				 * and memcpy being fast, rather than try to
				 * hand-optimized something specific.
				 */
				if (zerobuffer[0] != 0 || 
				    memcmp(zerobuffer, zerobuffer + 1, blockrun - 1)) {
					memcpy(dst, zerobuffer, blockrun);
					bit_set(map, bit);
					server_dirty(client->serve, from, blockrun);
					debug("non-zero, copied and set bit %d", bit);
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
	struct nbd_request_raw request_raw;
	fd_set                 fds;
	
	FD_ZERO(&fds);
	FD_SET(client->socket, &fds);
	FD_SET(client->serve->close_signal[0], &fds);
	CLIENT_ERROR_ON_FAILURE(select(FD_SETSIZE, &fds, NULL, NULL, NULL), 
	  "select() failed");
	
	if (FD_ISSET(client->serve->close_signal[0], &fds))
		return 0;

	if (readloop(client->socket, &request_raw, sizeof(request_raw)) == -1) {
		if (errno == 0) {
			debug("EOF reading request");
			return 0; /* neat point to close the socket */
		}
		else {
			CLIENT_ERROR_ON_FAILURE(-1, "Error reading request");
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

	CLIENT_ERROR_ON_FAILURE(
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
		CLIENT_ERROR("Bad magic %08x", request.magic);
		
	switch (request.type)
	{
	case REQUEST_READ:
		break;
	case REQUEST_WRITE:
		/* check it's not out of range */
		if (request.from < 0 || 
		    request.from+request.len > client->serve->size) {
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
		CLIENT_ERROR("Unknown request %08x", request.type);
	}
	return 1;
}


void client_reply_to_read( struct client* client, struct nbd_request request )
{
	off64_t offset;

	debug("request read %ld+%d", request.from, request.len);
	client_write_reply( client, &request, 0);

	offset = request.from;
	CLIENT_ERROR_ON_FAILURE(
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
	if (client->serve->block_allocation_map) {
		write_not_zeroes( client, request.from, request.len );
	}
	else {
		CLIENT_ERROR_ON_FAILURE(
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
		
		CLIENT_ERROR_ON_FAILURE(
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


int client_lock_io( struct client * client )
{
	CLIENT_ERROR_ON_FAILURE(
		pthread_mutex_lock(&client->serve->l_io),
		"Problem with I/O lock"
	);
	
	if (server_detect_closed(client->serve)) {
		CLIENT_ERROR_ON_FAILURE(
			pthread_mutex_unlock(&client->serve->l_io),
			"Problem with I/O unlock"
		);
		return 0;
	}

	return 1;
}


void client_unlock_io( struct client * client )
{
	CLIENT_ERROR_ON_FAILURE(
		pthread_mutex_unlock(&client->serve->l_io),
		"Problem with I/O unlock"
	);
}


int client_serve_request(struct client* client)
{
	struct nbd_request    request;
	int                   request_err;
		
	if ( !client_read_request( client, &request ) ) { return 1; }
	if ( !client_request_needs_reply( client, request, &request_err ) )  {
		return request_err;
	} 

	if ( client_lock_io( client ) ){
		client_reply( client, request );
		client_unlock_io( client );
	} else { 
		return 1; 
	}

	return 0;
}


void client_send_hello(struct client* client)
{
	client_write_init( client, client->serve->size );
}

void* client_serve(void* client_uncast)
{
	struct client* client = (struct client*) client_uncast;
	
	//client_open_file(client);
	CLIENT_ERROR_ON_FAILURE(
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
		
	CLIENT_ERROR_ON_FAILURE(
		close(client->socket),
		"Couldn't close socket %d", 
		client->socket
	);

	close(client->socket);
	close(client->fileno);
	munmap(client->mapped, client->serve->size);
	free(client);
	
	return NULL;
}
