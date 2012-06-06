#include "params.h"
#include "nbdtypes.h"
#include "ioutil.h"
#include "util.h"
#include "bitset.h"
#include "control.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/un.h>
#include <fcntl.h>

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/tcp.h>

static const int block_allocation_resolution = 4096;//128<<10;

static inline void dirty(struct mode_serve_params *serve, off64_t from, int len)
{
	if (serve->mirror)
		bitset_set_range(serve->mirror->dirty_map, from, len);
}

int server_detect_closed(struct mode_serve_params* serve)
{
	int errno_old = errno;
	int result = fcntl(serve->server_fd, F_GETFD, 0) < 0;
	errno = errno_old;
	return result;
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
void write_not_zeroes(struct client_params* client, off64_t from, int len)
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
			dirty(client->serve, from, run);
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
					dirty(client->serve, from, blockrun);
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
int client_read_request( struct client_params * client , struct nbd_request *out_request )
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
int client_write_reply( struct client_params * client, struct nbd_request *request, int error )
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

void client_write_init( struct client_params * client, uint64_t size )
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
int client_request_needs_reply( struct client_params * client, struct nbd_request request, int *request_err )
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


void client_reply_to_read( struct client_params* client, struct nbd_request request )
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


void client_reply_to_write( struct client_params* client, struct nbd_request request )
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
		dirty(client->serve, request.from, request.len);
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


void client_reply( struct client_params* client, struct nbd_request request )
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


int client_lock_io( struct client_params * client )
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


void client_unlock_io( struct client_params * client )
{
	CLIENT_ERROR_ON_FAILURE(
		pthread_mutex_unlock(&client->serve->l_io),
		"Problem with I/O unlock"
	);
}


int client_serve_request(struct client_params* client)
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


void client_send_hello(struct client_params* client)
{
	client_write_init( client, client->serve->size );
}

void* client_serve(void* client_uncast)
{
	struct client_params* client = (struct client_params*) client_uncast;
	
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

static int testmasks[9] = { 0,128,192,224,240,248,252,254,255 };

/** Test whether AF_INET or AF_INET6 sockaddr is included in the given access
  * control list, returning 1 if it is, and 0 if not.
  */
int is_included_in_acl(int list_length, struct ip_and_mask (*list)[], union mysockaddr* test)
{
	int i;
	
	for (i=0; i < list_length; i++) {
		struct ip_and_mask *entry = &(*list)[i];
		int testbits;
		unsigned char *raw_address1, *raw_address2;
		
		debug("checking acl entry %d (%d/%d)", i, test->generic.sa_family, entry->ip.family);
		
		if (test->generic.sa_family != entry->ip.family)
			continue;
		
		if (test->generic.sa_family == AF_INET) {
			debug("it's an AF_INET");
			raw_address1 = (unsigned char*) &test->v4.sin_addr;
			raw_address2 = (unsigned char*) &entry->ip.v4.sin_addr;
		}
		else if (test->generic.sa_family == AF_INET6) {
			debug("it's an AF_INET6");
			raw_address1 = (unsigned char*) &test->v6.sin6_addr;
			raw_address2 = (unsigned char*) &entry->ip.v6.sin6_addr;
		}
		
		debug("testbits=%d", entry->mask);
		
		for (testbits = entry->mask; testbits > 0; testbits -= 8) {
			debug("testbits=%d, c1=%02x, c2=%02x", testbits, raw_address1[0], raw_address2[0]);
			if (testbits >= 8) {
				if (raw_address1[0] != raw_address2[0])
					goto no_match;
			}
			else {
				if ((raw_address1[0] & testmasks[testbits%8]) !=
				    (raw_address2[0] & testmasks[testbits%8]) )
				    	goto no_match;
			}
			
			raw_address1++;
			raw_address2++;
		}
		
		return 1;
		
		no_match: ;
		debug("no match");
	}
	
	return 0;
}

/** Prepares a listening socket for the NBD server, binding etc. */
void serve_open_server_socket(struct mode_serve_params* params)
{
	int optval=1;
	
	params->server_fd= socket(params->bind_to.generic.sa_family == AF_INET ? 
	  PF_INET : PF_INET6, SOCK_STREAM, 0);
	
	SERVER_ERROR_ON_FAILURE(params->server_fd, 
	  "Couldn't create server socket");

	SERVER_ERROR_ON_FAILURE(
		setsockopt(params->server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)),
		"Couldn't set SO_REUSEADDR"
	);

	SERVER_ERROR_ON_FAILURE(
		setsockopt(params->server_fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)),
		"Couldn't set TCP_NODELAY"
	);

	SERVER_ERROR_ON_FAILURE(
		bind(params->server_fd, &params->bind_to.generic,
		  sizeof(params->bind_to)),
		"Couldn't bind server to IP address"
	);
	
	SERVER_ERROR_ON_FAILURE(
		listen(params->server_fd, params->tcp_backlog),
		"Couldn't listen on server socket"
	);
}

/** We can only accommodate MAX_NBD_CLIENTS connections at once.  This function
 *  goes through the current list, waits for any threads that have finished
 *  and returns the next slot free (or -1 if there are none).
 */
int cleanup_and_find_client_slot(struct mode_serve_params* params)
{
	int slot=-1, i;
	
	for (i=0; i < MAX_NBD_CLIENTS; i++) {
		void* status;
		
		if (params->nbd_client[i].thread != 0) {
			char s_client_address[64];
			
			memset(s_client_address, 0, 64);
			strcpy(s_client_address, "???");
			inet_ntop(
				params->nbd_client[i].address.generic.sa_family, 
				sockaddr_address_data(&params->nbd_client[i].address.generic), 
                                    s_client_address, 
				64
			);
			
			if (pthread_tryjoin_np(params->nbd_client[i].thread, &status) < 0) {
				if (errno != EBUSY)
					SERVER_ERROR_ON_FAILURE(-1, "Problem with joining thread");
			}
			else {
				params->nbd_client[i].thread = 0;
				debug("nbd thread %d exited (%s) with status %ld", (int) params->nbd_client[i].thread, s_client_address, (uint64_t)status);
			}
		}
		
		if (params->nbd_client[i].thread == 0 && slot == -1)
			slot = i;
	}
	
	return slot;
}

/** Dispatch function for accepting an NBD connection and starting a thread
  * to handle it.  Rejects the connection if there is an ACL, and the far end's
  * address doesn't match, or if there are too many clients already connected.
  */
void accept_nbd_client(
		struct mode_serve_params* params, 
		int client_fd, 
		union mysockaddr* client_address)
{
	struct client_params* client_params;
	int slot = cleanup_and_find_client_slot(params); 
	char s_client_address[64];
	int acl_passed = 0;

	
	if (inet_ntop(client_address->generic.sa_family,
				sockaddr_address_data(&client_address->generic),
				s_client_address, 64) == NULL) {
		write(client_fd, "Bad client_address", 18);
		close(client_fd);
		return;
	}
	

	if (params->acl) {
		if (is_included_in_acl(params->acl_entries, params->acl, client_address))
			acl_passed = 1;
	} else {
		if (!params->default_deny)
			acl_passed = 1;
	}

	if (!acl_passed) {
 		write(client_fd, "Access control error", 20);
 		close(client_fd);
 		return;
	}

	
	if (slot < 0) {
		write(client_fd, "Too many clients", 16);
		close(client_fd);
		return;
	}
	
	client_params = xmalloc(sizeof(struct client_params));
	client_params->socket = client_fd;
	client_params->serve = params;
	
	if (pthread_create(&params->nbd_client[slot].thread, NULL, client_serve, client_params) < 0) {
		write(client_fd, "Thread creation problem", 23);
		free(client_params);
		close(client_fd);
		return;
	}
	
	memcpy(&params->nbd_client[slot].address, client_address, 
	  sizeof(union mysockaddr));
	
	debug("nbd thread %d started (%s)", (int) params->nbd_client[slot].thread, s_client_address);
}

/** Accept either an NBD or control socket connection, dispatch appropriately */
void serve_accept_loop(struct mode_serve_params* params) 
{
	while (1) {
		int              activity_fd, client_fd;
		union mysockaddr client_address;
		fd_set           fds;
		socklen_t        socklen=sizeof(client_address);
		
		FD_ZERO(&fds);
		FD_SET(params->server_fd, &fds);
		FD_SET(params->close_signal[0], &fds);
		if (params->control_socket_name)
			FD_SET(params->control, &fds);
		
		SERVER_ERROR_ON_FAILURE(select(FD_SETSIZE, &fds, 
		  NULL, NULL, NULL), "select() failed");
		
		if (FD_ISSET(params->close_signal[0], &fds))
			return;
		
		activity_fd = FD_ISSET(params->server_fd, &fds) ? params->server_fd: 
		  params->control;
		client_fd = accept(activity_fd, &client_address.generic, &socklen);
		
		SERVER_ERROR_ON_FAILURE(
			pthread_mutex_lock(&params->l_accept),
			"Problem with accept lock"
		);
		
		if (activity_fd == params->server_fd)
			accept_nbd_client(params, client_fd, &client_address);
		if (activity_fd == params->control)
			accept_control_connection(params, client_fd, &client_address);
			
		SERVER_ERROR_ON_FAILURE(
			pthread_mutex_unlock(&params->l_accept),
			"Problem with accept unlock"
		);
	}
}

/** Initialisation function that sets up the initial allocation map, i.e. so
  * we know which blocks of the file are allocated.
  */
void serve_init_allocation_map(struct mode_serve_params* params)
{
	int fd = open(params->filename, O_RDONLY);
	off64_t size;
	SERVER_ERROR_ON_FAILURE(fd, "Couldn't open %s", params->filename);
	size = lseek64(fd, 0, SEEK_END);
	params->size = size;
	SERVER_ERROR_ON_FAILURE(size, "Couldn't find size of %s", 
	  params->filename);
	params->block_allocation_map = 
		build_allocation_map(fd, size, block_allocation_resolution);
	close(fd);
}

/** Closes sockets, frees memory and waits for all client threads to finish */
void serve_cleanup(struct mode_serve_params* params)
{
	int i;
	
	close(params->server_fd);
	close(params->control);
	if (params->acl)
		free(params->acl);
	//free(params->filename);
	if (params->control_socket_name)
		//free(params->control_socket_name);
	pthread_mutex_destroy(&params->l_accept);
	pthread_mutex_destroy(&params->l_io);
	if (params->proxy_fd);
		close(params->proxy_fd);
	close(params->close_signal[0]);
	close(params->close_signal[1]);
	free(params->block_allocation_map);
	
	if (params->mirror)
		debug("mirror thread running! this should not happen!");
	
	for (i=0; i < MAX_NBD_CLIENTS; i++) {
		void* status;
		
		if (params->nbd_client[i].thread != 0) {
			debug("joining thread %d", i);
			pthread_join(params->nbd_client[i].thread, &status);
		}
	}
}

/** Full lifecycle of the server */
void do_serve(struct mode_serve_params* params)
{
	pthread_mutex_init(&params->l_accept, NULL);
	pthread_mutex_init(&params->l_io, NULL);
	SERVER_ERROR_ON_FAILURE(pipe(params->close_signal) , "pipe failed");
	
	serve_open_server_socket(params);
	serve_open_control_socket(params);
	serve_init_allocation_map(params);
	serve_accept_loop(params);
	serve_cleanup(params);
}

