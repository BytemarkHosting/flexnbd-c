#ifndef PROXY_H
#define PROXY_H

#include <sys/types.h>
#include <unistd.h>

#include "ioutil.h"
#include "flexnbd.h"
#include "parse.h"
#include "nbdtypes.h"
#include "self_pipe.h"

#ifdef PREFETCH
  #include "prefetch.h"
#endif

struct proxier {
	/* The flexnbd wrapper this proxier is attached to */
	struct flexnbd*   flexnbd;

	/** address/port to bind to */
	union mysockaddr  listen_on;

	/** address/port to connect to */
	union mysockaddr  connect_to;

	/** address to bind to when making outgoing connections */
	union mysockaddr  connect_from;
	int               bind; /* Set to true if we should use it */

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

	/* We transform the raw request header into here */
	struct nbd_request req_hdr;

    /* We transform the raw reply header into here */
	struct nbd_reply   rsp_hdr;

	/* Used for our non-blocking negotiation with upstream. TODO: maybe use
	 * for downstream as well ( we currently overload rsp ) */
	struct iobuf init;

	/* The current NBD request from downstream */
	struct iobuf req;

	/* The current NBD reply from upstream */
	struct iobuf rsp;

	/* It's starting to feel like we need an object for a single proxy session.
	 * These two track how many requests we've sent so far, and whether the
	 * NBD_INIT code has been sent to the client yet.
	 */
	uint64_t req_count;
	int hello_sent;

#ifdef PREFETCH
	struct prefetch *prefetch;
#endif
};

struct proxier* proxy_create(
	char* s_downstream_address,
	char* s_downstream_port,
	char* s_upstream_address,
	char* s_upstream_port,
	char* s_upstream_bind );
int do_proxy( struct proxier* proxy );
void proxy_cleanup( struct proxier* proxy );
void proxy_destroy( struct proxier* proxy );

#endif

