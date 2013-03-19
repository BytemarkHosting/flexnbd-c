#ifndef PROXY_H
#define PROXY_H

#include <sys/types.h>
#include <unistd.h>

#include "flexnbd.h"
#include "parse.h"
#include "nbdtypes.h"
#include "self_pipe.h"

struct proxier {
	/* The flexnbd wrapper this proxier is attached to */
	struct flexnbd*   flexnbd;

	/** address/port to bind to */
	union mysockaddr  listen_on;

	/** address/port to connect to */
	union mysockaddr  connect_to;

	/** address to bind to when making outgoing connections */
	union mysockaddr  connect_from;

	/* The socket we listen() on and accept() against */
	int               listen_fd;

	/* The socket returned by accept() that we receive requests from and send
	 * responses to
	 */
	int               downstream_fd;

	/* The socket returned by connect() that we send requests to and receive
	 * responses from
	 */
	int               upstream_fd;

	/* This is the size we advertise to the downstream server */
	off64_t           upstream_size;

	/* Scratch space for the current NBD request from downstream */
	unsigned char*     req_buf;

	/* Number of bytes currently sat in req_buf */
	size_t             req_buf_size;

	/* We transform the raw request header into here */
	struct nbd_request req_hdr;

	/* Scratch space for the current NBD reply from upstream */
	unsigned char*     rsp_buf;

	/* Number of bytes currently sat in rsp_buf */
	size_t             rsp_buf_size;

    /* We transform the raw reply header into here */
	struct nbd_reply   rsp_hdr;

	/* File descriptor that signal handlers write to */
	int signal_fd;
};

struct proxier* proxy_create(
	int signal_fd,
	char* s_downstream_address,
	char* s_downstream_port,
	char* s_upstream_address,
	char* s_upstream_port,
	char* s_upstream_bind );
int do_proxy( struct proxier* proxy );
void proxy_destroy( struct proxier* proxy );

#endif

