#ifndef PROXY_H
#define PROXY_H

#include <sys/types.h>
#include <unistd.h>

#include "ioutil.h"
#include "parse.h"
#include "nbdtypes.h"
#include "self_pipe.h"

#ifdef PREFETCH
  #include "prefetch.h"
#endif

/** UPSTREAM_TIMEOUT
 * How long ( in ms ) to allow for upstream to respond. If it takes longer
 * than this, we will cancel the current request-response to them and resubmit
 */
#define UPSTREAM_TIMEOUT 30 * 1000

struct proxier {
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
	uint64_t          upstream_size;

	/* These are the transmission flags sent as part of the handshake */
	uint32_t          upstream_flags;

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

	/** These are only used if we pass --cache on the command line */

	/* While the in-flight request has been munged by prefetch, these two are
	 * set to true, and the original length of the request, respectively */
	int is_prefetch_req;
	uint32_t prefetch_req_orig_len;

	/* And here, we actually store the prefetched data once it's returned */
	struct prefetch *prefetch;

	/** */
};

struct proxier* proxy_create(
	char* s_downstream_address,
	char* s_downstream_port,
	char* s_upstream_address,
	char* s_upstream_port,
	char* s_upstream_bind,
	char* s_cache_bytes);
int do_proxy( struct proxier* proxy );
void proxy_cleanup( struct proxier* proxy );
void proxy_destroy( struct proxier* proxy );

#endif

