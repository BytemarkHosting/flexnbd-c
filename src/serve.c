#include "serve.h"
#include "client.h"
#include "nbdtypes.h"
#include "ioutil.h"
#include "util.h"
#include "bitset.h"
#include "control.h"
#include "self_pipe.h"

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

static inline void* sockaddr_address_data(struct sockaddr* sockaddr)
{
	struct sockaddr_in*  in  = (struct sockaddr_in*) sockaddr;
	struct sockaddr_in6* in6 = (struct sockaddr_in6*) sockaddr;
	
	if (sockaddr->sa_family == AF_INET)
		return &in->sin_addr;
	if (sockaddr->sa_family == AF_INET6)
		return &in6->sin6_addr;
	return NULL;
}

void server_dirty(struct server *serve, off64_t from, int len)
{
	if (serve->mirror)
		bitset_set_range(serve->mirror->dirty_map, from, len);
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
void serve_open_server_socket(struct server* params)
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
int cleanup_and_find_client_slot(struct server* params)
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
		struct server* params, 
		int client_fd, 
		union mysockaddr* client_address)
{
	struct client* client_params;
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
	
	client_params = xmalloc(sizeof(struct client));
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


int server_detect_closed(struct server* serve)
{
	int errno_old = errno;
	int result = fcntl(serve->server_fd, F_GETFD, 0) < 0;
	errno = errno_old;
	return result;
}


/** Accept either an NBD or control socket connection, dispatch appropriately */
void serve_accept_loop(struct server* params) 
{
	while (1) {
		int              activity_fd, client_fd;
		union mysockaddr client_address;
		fd_set           fds;
		socklen_t        socklen=sizeof(client_address);
		
		FD_ZERO(&fds);
		FD_SET(params->server_fd, &fds);
		self_pipe_fd_set( params->close_signal, &fds );
		if (params->control_socket_name)
			FD_SET(params->control_fd, &fds);
		
		SERVER_ERROR_ON_FAILURE(select(FD_SETSIZE, &fds, 
		  NULL, NULL, NULL), "select() failed");
		
		if ( self_pipe_fd_isset( params->close_signal, &fds) )
			return;
		
		activity_fd = FD_ISSET(params->server_fd, &fds) ? params->server_fd: 
		  params->control_fd;
		client_fd = accept(activity_fd, &client_address.generic, &socklen);
		
		SERVER_ERROR_ON_FAILURE(
			pthread_mutex_lock(&params->l_accept),
			"Problem with accept lock"
		);
		
		if (activity_fd == params->server_fd)
			accept_nbd_client(params, client_fd, &client_address);
		if (activity_fd == params->control_fd)
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
void serve_init_allocation_map(struct server* params)
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


/* Tell the server to close all the things. */
void serve_signal_close( struct server * serve )
{
	self_pipe_signal( serve->close_signal );
}


/** Closes sockets, frees memory and waits for all client threads to finish */
void serve_cleanup(struct server* params)
{
	int i;
	
	close(params->server_fd);
	close(params->control_fd);
	if (params->acl)
		free(params->acl);
	//free(params->filename);
	if (params->control_socket_name)
		//free(params->control_socket_name);
	pthread_mutex_destroy(&params->l_accept);
	pthread_mutex_destroy(&params->l_io);
	if (params->proxy_fd);
		close(params->proxy_fd);

	self_pipe_destroy( params->close_signal );

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
void do_serve(struct server* params)
{
	pthread_mutex_init(&params->l_accept, NULL);
	pthread_mutex_init(&params->l_io, NULL);

	params->close_signal = self_pipe_create();
	if ( NULL == params->close_signal) { SERVER_ERROR( "pipe failed" ); }
	
	serve_open_server_socket(params);
	serve_open_control_socket(params);
	serve_init_allocation_map(params);
	serve_accept_loop(params);
	serve_cleanup(params);
}

