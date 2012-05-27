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
	/* address/port to bind to */
	union mysockaddr     bind_to;
	/* number of entries in current access control list*/
	int                  acl_entries;
	/* pointer to access control list entries*/
	struct ip_and_mask   (*acl)[0];
	/* file name to serve */
	char*                filename;
	/* TCP backlog for listen() */
	int                  tcp_backlog;
	/* file name of UNIX control socket (or NULL if none) */
	char*                control_socket_name;
	/* size of file */
	off64_t              size;
	/* if you want the main thread to pause, set this to an writeable
	 * file descriptor.  The main thread will then write a byte once it
	 * promises to hang any further writes.
	 */
	int                  pause_fd;
	/* the main thread will set this when writes will be paused */
	int                  paused;
	/* set to non-zero to use given destination connection as proxy */
	int                  proxy_fd;

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

