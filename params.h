#ifndef __PARAMS_H
#define __PARAMS_H

#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE

#include "parse.h"

#include <sys/types.h>

struct mode_serve_params {
	union mysockaddr     bind_to;
	int                  acl_entries;
	struct ip_and_mask  *acl[0];
	char*                filename;
	int                  tcp_backlog;
	char*                control_socket_name;
	
	int                  server;
	int                  control;
	
	char*                block_allocation_map;
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
	char*   filename;
	
	int     fileno;
	off64_t size;
	char*   mapped;
	
	char*                block_allocation_map;
};

struct control_params {
	int                       socket;
	struct mode_serve_params* serve;
};

union mode_params {
	struct mode_serve_params serve;
	struct mode_readwrite_params readwrite;
};

#endif

