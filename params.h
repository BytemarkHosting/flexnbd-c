#ifndef __PARAMS_H
#define __PARAMS_H

#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

union mysockaddr {
	unsigned short      family;
	struct sockaddr     generic;
        struct sockaddr_in  v4;
        struct sockaddr_in6 v6;
};

struct ip_and_mask {
	union mysockaddr ip;
	int              mask;
};

struct mode_serve_params {
	union mysockaddr     bind_to;
	int                  acl_entries;
	struct ip_and_mask** acl;
	char*                filename;
	int                  tcp_backlog;
	
	int                  server;
	int                  threads;
	
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

union mode_params {
	struct mode_serve_params serve;
	struct mode_readwrite_params readwrite;
};

#endif

