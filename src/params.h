#ifndef __PARAMS_H
#define __PARAMS_H

#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE

#include "parse.h"

#include <sys/types.h>

enum mirror_finish_action {
	ACTION_PROXY,
	ACTION_EXIT,
	ACTION_NOTHING
};

struct mirror_status {
	pthread_t            thread;
	int                  client;
	char                 *filename;
	off64_t              max_bytes_per_second;
	enum mirror_finish_action action_at_finish;
	
	char                 *mapped;
	struct bitset_mapping *dirty_map;
};

struct control_params {
	int                       socket;
	struct mode_serve_params* serve;
};

#define MAX_NBD_CLIENTS 16
struct mode_serve_params {
	/** address/port to bind to */
	union mysockaddr     bind_to;
	/** number of entries in current access control list*/
	int                  acl_entries;
	/** pointer to access control list entries*/
	struct ip_and_mask   (*acl)[0];
	/** (static) file name to serve */
	char*                filename;
	/** file name of INCOMPLETE flag */
	char*                filename_incomplete;
	/** TCP backlog for listen() */
	int                  tcp_backlog;
	/** (static) file name of UNIX control socket (or NULL if none) */
	char*                control_socket_name;
	/** size of file */
	off64_t              size;

        /* NB dining philosophers if we ever mave more than one thread 
         * that might need to pause the whole server.  At the moment we only
         * have the one.
         */
       
	/** Claimed around any accept/thread starting loop */
	pthread_mutex_t      l_accept; 
	/** Claims around any I/O to this file */
	pthread_mutex_t      l_io;
	
	/** set to non-zero to cause r/w requests to go via this fd */
	int                  proxy_fd;
	
	/** to interrupt accept loop and clients, write() to close_signal[1] */
	int                  close_signal[2];

	struct mirror_status* mirror;
	int                  server;
	int                  control;
	
	char*                block_allocation_map;
	
	struct { pthread_t thread; struct sockaddr address; }
	                     nbd_client[MAX_NBD_CLIENTS];
};

struct mode_readwrite_params {
	union mysockaddr     connect_to;
	off64_t              from;
	off64_t              len;
	int                  data_fd;
	int                  client;
};

struct client_params {
	int     socket;
	
	int     fileno;
	char*   mapped;
	
	struct mode_serve_params* serve; /* FIXME: remove above duplication */
};

/* FIXME: wrong place */
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

#endif

