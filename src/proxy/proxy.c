#include "proxy.h"
#include "readwrite.h"

#include "prefetch.h"


#include "ioutil.h"
#include "sockutil.h"
#include "util.h"

#include <errno.h>

#include <sys/socket.h>
#include <netinet/tcp.h>

struct proxier *proxy_create(char *s_downstream_address,
			     char *s_downstream_port,
			     char *s_upstream_address,
			     char *s_upstream_port,
			     char *s_upstream_bind, char *s_cache_bytes)
{
    struct proxier *out;
    out = xmalloc(sizeof(struct proxier));

    FATAL_IF_NULL(s_downstream_address, "Listen address not specified");
    NULLCHECK(s_downstream_address);

    FATAL_UNLESS(parse_to_sockaddr
		 (&out->listen_on.generic, s_downstream_address),
		 "Couldn't parse downstream address %s");

    if (out->listen_on.family != AF_UNIX) {
	FATAL_IF_NULL(s_downstream_port, "Downstream port not specified");
	NULLCHECK(s_downstream_port);
	parse_port(s_downstream_port, &out->listen_on.v4);
    }

    FATAL_IF_NULL(s_upstream_address, "Upstream address not specified");
    NULLCHECK(s_upstream_address);

    FATAL_UNLESS(parse_ip_to_sockaddr
		 (&out->connect_to.generic, s_upstream_address),
		 "Couldn't parse upstream address '%s'",
		 s_upstream_address);

    FATAL_IF_NULL(s_upstream_port, "Upstream port not specified");
    NULLCHECK(s_upstream_port);
    parse_port(s_upstream_port, &out->connect_to.v4);

    if (s_upstream_bind) {
	FATAL_IF_ZERO(parse_ip_to_sockaddr
		      (&out->connect_from.generic, s_upstream_bind),
		      "Couldn't parse bind address '%s'", s_upstream_bind);
	out->bind = 1;
    }

    out->listen_fd = -1;
    out->downstream_fd = -1;
    out->upstream_fd = -1;

    out->prefetch = NULL;
    if (s_cache_bytes) {
	int cache_bytes = atoi(s_cache_bytes);
	/* leaving this off or setting a cache size of zero or
	 * less results in no cache.
	 */
	if (cache_bytes >= 0) {
	    out->prefetch = prefetch_create(cache_bytes);
	}
    }

    out->init.buf = xmalloc(sizeof(struct nbd_init_raw));

    /* Add on the request / reply size to our malloc to accommodate both
     * the struct and the data
     */
    out->req.buf = xmalloc(NBD_MAX_SIZE + NBD_REQUEST_SIZE);
    out->rsp.buf = xmalloc(NBD_MAX_SIZE + NBD_REPLY_SIZE);

    log_context =
	xmalloc(strlen(s_upstream_address) + strlen(s_upstream_port) + 2);
    sprintf(log_context, "%s:%s", s_upstream_address, s_upstream_port);

    return out;
}

int proxy_prefetches(struct proxier *proxy)
{
    NULLCHECK(proxy);
    return proxy->prefetch != NULL;
}

int proxy_prefetch_bufsize(struct proxier *proxy)
{
    NULLCHECK(proxy);
    return prefetch_size(proxy->prefetch);
}

void proxy_destroy(struct proxier *proxy)
{
    free(proxy->init.buf);
    free(proxy->req.buf);
    free(proxy->rsp.buf);
    prefetch_destroy(proxy->prefetch);

    free(proxy);
}

/* Shared between our two different connect_to_upstream paths */
void proxy_finish_connect_to_upstream(struct proxier *proxy, uint64_t size,
				      uint32_t flags);

/* Try to establish a connection to our upstream server. Return 1 on success,
 * 0 on failure. this is a blocking call that returns a non-blocking socket.
 */
int proxy_connect_to_upstream(struct proxier *proxy)
{
    struct sockaddr *connect_from = NULL;
    if (proxy->bind) {
	connect_from = &proxy->connect_from.generic;
    }

    int fd = socket_connect(&proxy->connect_to.generic, connect_from);
    uint64_t size = 0;
    uint32_t flags = 0;

    if (-1 == fd) {
	return 0;
    }

    if (!socket_nbd_read_hello(fd, &size, &flags)) {
	WARN_IF_NEGATIVE(sock_try_close(fd),
			 "Couldn't close() after failed read of NBD hello on fd %i",
			 fd);
	return 0;
    }

    proxy->upstream_fd = fd;
    sock_set_nonblock(fd, 1);
    proxy_finish_connect_to_upstream(proxy, size, flags);

    return 1;
}

/* First half of non-blocking connection to upstream. Gets as far as calling
 * connect() on a non-blocking socket.
 */
void proxy_start_connect_to_upstream(struct proxier *proxy)
{
    int fd, result;
    struct sockaddr *from = NULL;
    struct sockaddr *to = &proxy->connect_to.generic;

    if (proxy->bind) {
	from = &proxy->connect_from.generic;
    }

    fd = socket(to->sa_family, SOCK_STREAM, 0);

    if (fd < 0) {
	warn(SHOW_ERRNO
	     ("Couldn't create socket to reconnect to upstream"));
	return;
    }

    info("Beginning non-blocking connection to upstream on fd %i", fd);

    if (NULL != from) {
	if (0 > bind(fd, from, sockaddr_size(from))) {
	    warn(SHOW_ERRNO("bind() to source address failed"));
	}
    }

    result = sock_set_nonblock(fd, 1);
    if (result == -1) {
	warn(SHOW_ERRNO("Failed to set upstream fd %i non-blocking", fd));
	goto error;
    }

    result = connect(fd, to, sockaddr_size(to));
    if (result == -1 && errno != EINPROGRESS) {
	warn(SHOW_ERRNO("Failed to start connect()ing to upstream!"));
	goto error;
    }

    proxy->upstream_fd = fd;
    return;

  error:
    if (sock_try_close(fd) == -1) {
	/* Non-fatal leak, although still nasty */
	warn(SHOW_ERRNO("Failed to close fd for upstream %i", fd));
    }
    return;
}

void proxy_finish_connect_to_upstream(struct proxier *proxy, uint64_t size,
				      uint32_t flags)
{

    if (proxy->upstream_size == 0) {
	info("Size of upstream image is %" PRIu64 " bytes", size);
    } else if (proxy->upstream_size != size) {
	warn("Size changed from %" PRIu64 " to %" PRIu64 " bytes",
	     proxy->upstream_size, size);
    }

    proxy->upstream_size = size;

    if (proxy->upstream_flags == 0) {
	info("Upstream transmission flags set to %" PRIu32 "", flags);
    } else if (proxy->upstream_flags != flags) {
	warn("Upstream transmission flags changed from %" PRIu32 " to %"
	     PRIu32 "", proxy->upstream_flags, flags);
    }

    proxy->upstream_flags = flags;

    if (AF_UNIX != proxy->connect_to.family) {
	if (sock_set_tcp_nodelay(proxy->upstream_fd, 1) == -1) {
	    warn(SHOW_ERRNO("Failed to set TCP_NODELAY"));
	}
    }

    info("Connected to upstream on fd %i", proxy->upstream_fd);

    return;
}

void proxy_disconnect_from_upstream(struct proxier *proxy)
{
    if (-1 != proxy->upstream_fd) {
	info("Closing upstream connection on fd %i", proxy->upstream_fd);

	/* TODO: An NBD disconnect would be pleasant here */
	WARN_IF_NEGATIVE(sock_try_close(proxy->upstream_fd),
			 "Failed to close() fd %i when disconnecting from upstream",
			 proxy->upstream_fd);
	proxy->upstream_fd = -1;
    }
}


/** Prepares a listening socket for the NBD server, binding etc. */
void proxy_open_listen_socket(struct proxier *params)
{
    NULLCHECK(params);

    params->listen_fd = socket(params->listen_on.family, SOCK_STREAM, 0);
    FATAL_IF_NEGATIVE(params->listen_fd,
		      SHOW_ERRNO("Couldn't create listen socket")
	);

    /* Allow us to restart quickly */
    FATAL_IF_NEGATIVE(sock_set_reuseaddr(params->listen_fd, 1),
		      SHOW_ERRNO("Couldn't set SO_REUSEADDR")
	);

    if (AF_UNIX != params->listen_on.family) {
	FATAL_IF_NEGATIVE(sock_set_tcp_nodelay(params->listen_fd, 1),
			  SHOW_ERRNO("Couldn't set TCP_NODELAY")
	    );
    }

    FATAL_UNLESS_ZERO(sock_try_bind
		      (params->listen_fd, &params->listen_on.generic),
		      SHOW_ERRNO("Failed to bind to listening socket")
	);

    /* We're only serving one client at a time, hence backlog of 1 */
    FATAL_IF_NEGATIVE(listen(params->listen_fd, 1),
		      SHOW_ERRNO("Failed to listen on listening socket")
	);

    info("Now listening for incoming connections");

    return;
}

typedef enum {
    EXIT,
    WRITE_TO_DOWNSTREAM,
    READ_FROM_DOWNSTREAM,
    CONNECT_TO_UPSTREAM,
    READ_INIT_FROM_UPSTREAM,
    WRITE_TO_UPSTREAM,
    READ_FROM_UPSTREAM
} proxy_session_states;

static char *proxy_session_state_names[] = {
    "EXIT",
    "WRITE_TO_DOWNSTREAM",
    "READ_FROM_DOWNSTREAM",
    "CONNECT_TO_UPSTREAM",
    "READ_INIT_FROM_UPSTREAM",
    "WRITE_TO_UPSTREAM",
    "READ_FROM_UPSTREAM"
};

static inline int proxy_state_upstream(int state)
{
    return state == CONNECT_TO_UPSTREAM || state == READ_INIT_FROM_UPSTREAM
	|| state == WRITE_TO_UPSTREAM || state == READ_FROM_UPSTREAM;
}

int proxy_prefetch_for_request(struct proxier *proxy, int state)
{
    NULLCHECK(proxy);
    struct nbd_request *req = &proxy->req_hdr;
    struct nbd_reply *rsp = &proxy->rsp_hdr;

    struct nbd_request_raw *req_raw =
	(struct nbd_request_raw *) proxy->req.buf;
    struct nbd_reply_raw *rsp_raw =
	(struct nbd_reply_raw *) proxy->rsp.buf;

    int is_read = req->type == REQUEST_READ;

    if (is_read) {
	/* See if we can respond with what's in our prefetch
	 * cache */
	if (prefetch_is_full(proxy->prefetch) &&
	    prefetch_contains(proxy->prefetch, req->from, req->len)) {
	    /* HUZZAH!  A match! */
	    debug("Prefetch hit!");

	    /* First build a reply header */
	    rsp->magic = REPLY_MAGIC;
	    rsp->error = 0;
	    memcpy(&rsp->handle, &req->handle, 8);

	    /* now copy it into the response */
	    nbd_h2r_reply(rsp, rsp_raw);

	    /* and the data */
	    memcpy(proxy->rsp.buf + NBD_REPLY_SIZE,
		   prefetch_offset(proxy->prefetch, req->from), req->len);

	    proxy->rsp.size = NBD_REPLY_SIZE + req->len;
	    proxy->rsp.needle = 0;

	    /* return early, our work here is done */
	    return WRITE_TO_DOWNSTREAM;
	}
    } else {
	/* Safety catch.  If we're sending a write request, we
	 * blow away the cache.  This is very pessimistic, but
	 * it's simpler (and therefore safer) than working out
	 * whether we can keep it or not.
	 */
	debug("Blowing away prefetch cache on type %d request.",
	      req->type);
	prefetch_set_is_empty(proxy->prefetch);
    }

    debug("Prefetch cache MISS!");

    uint64_t prefetch_start = req->from;
    /* We prefetch what we expect to be the next request. */
    uint64_t prefetch_end = req->from + (req->len * 2);

    /* We only want to consider prefetching if we know we're not
     * getting too much data back, if it's a read request, and if
     * the prefetch won't try to read past the end of the file.
     */
    int prefetching =
	req->len <= prefetch_size(proxy->prefetch) &&
	is_read &&
	prefetch_start < prefetch_end &&
	prefetch_end <= proxy->upstream_size;

    /* We pull the request out of the proxy struct, rewrite the
     * request size, and write it back.
     */
    if (prefetching) {
	proxy->is_prefetch_req = 1;
	proxy->prefetch_req_orig_len = req->len;

	req->len *= 2;

	debug("Prefetching additional %" PRIu32 " bytes",
	      req->len - proxy->prefetch_req_orig_len);
	nbd_h2r_request(req, req_raw);
    }

    return state;
}

int proxy_prefetch_for_reply(struct proxier *proxy, int state)
{
    size_t prefetched_bytes;

    if (!proxy->is_prefetch_req) {
	return state;
    }

    prefetched_bytes = proxy->req_hdr.len - proxy->prefetch_req_orig_len;

    debug("Prefetched additional %d bytes", prefetched_bytes);
    memcpy(proxy->prefetch->buffer,
	   proxy->rsp.buf + proxy->prefetch_req_orig_len + NBD_REPLY_SIZE,
	   prefetched_bytes);

    proxy->prefetch->from =
	proxy->req_hdr.from + proxy->prefetch_req_orig_len;
    proxy->prefetch->len = prefetched_bytes;

    /* We've finished with proxy->req by now, so don't need to alter it to make
     * it look like the request was before prefetch */

    /* Truncate the bytes we'll write downstream */
    proxy->req_hdr.len = proxy->prefetch_req_orig_len;
    proxy->rsp.size -= prefetched_bytes;

    /* And we need to reset these */
    prefetch_set_is_full(proxy->prefetch);
    proxy->is_prefetch_req = 0;

    return state;
}



int proxy_read_from_downstream(struct proxier *proxy, int state)
{
    ssize_t count;

    struct nbd_request_raw *request_raw =
	(struct nbd_request_raw *) proxy->req.buf;
    struct nbd_request *request = &(proxy->req_hdr);

//      assert( state == READ_FROM_DOWNSTREAM );

    count =
	iobuf_read(proxy->downstream_fd, &proxy->req, NBD_REQUEST_SIZE);

    if (count == -1) {
	warn(SHOW_ERRNO("Couldn't read request from downstream"));
	return EXIT;
    }

    if (proxy->req.needle == NBD_REQUEST_SIZE) {
	nbd_r2h_request(request_raw, request);

	if (request->type == REQUEST_DISCONNECT) {
	    info("Received disconnect request from client");
	    return EXIT;
	}

	/* Simple validations -- the request / reply size have already
	 * been taken into account in the xmalloc, so no need to worry
	 * about them here
	 */
	if (request->type == REQUEST_READ) {
	    if (request->len > NBD_MAX_SIZE) {
		warn("NBD read request size %" PRIu32 " too large",
		     request->len);
		return EXIT;
	    }
	}
	if (request->type == REQUEST_WRITE) {
	    if (request->len > NBD_MAX_SIZE) {
		warn("NBD write request size %" PRIu32 " too large",
		     request->len);
		return EXIT;
	    }

	    proxy->req.size += request->len;
	}
    }

    if (proxy->req.needle == proxy->req.size) {
	debug("Received NBD request from downstream. type=%" PRIu16
	      " flags=%" PRIu16 " from=%" PRIu64 " len=%" PRIu32,
	      request->type, request->flags, request->from, request->len);

	/* Finished reading, so advance state. Leave size untouched so the next
	 * state knows how many bytes to write */
	proxy->req.needle = 0;
	return WRITE_TO_UPSTREAM;
    }

    return state;
}

int proxy_continue_connecting_to_upstream(struct proxier *proxy, int state)
{
    int error, result;
    socklen_t len = sizeof(error);

//      assert( state == CONNECT_TO_UPSTREAM );

    result =
	getsockopt(proxy->upstream_fd, SOL_SOCKET, SO_ERROR, &error, &len);

    if (result == -1) {
	warn(SHOW_ERRNO("Failed to tell if connected to upstream"));
	return state;
    }

    if (error != 0) {
	errno = error;
	warn(SHOW_ERRNO("Failed to connect to upstream"));
	return state;
    }

    /* Data may have changed while we were disconnected */
    prefetch_set_is_empty(proxy->prefetch);

    info("Connected to upstream on fd %i", proxy->upstream_fd);
    return READ_INIT_FROM_UPSTREAM;
}

int proxy_read_init_from_upstream(struct proxier *proxy, int state)
{
    ssize_t count;

//      assert( state == READ_INIT_FROM_UPSTREAM );

    count =
	iobuf_read(proxy->upstream_fd, &proxy->init,
		   sizeof(struct nbd_init_raw));

    if (count == -1) {
	warn(SHOW_ERRNO("Failed to read init from upstream"));
	goto disconnect;
    }

    if (proxy->init.needle == proxy->init.size) {
	uint64_t upstream_size;
	uint32_t upstream_flags;
	if (!nbd_check_hello
	    ((struct nbd_init_raw *) proxy->init.buf, &upstream_size,
	     &upstream_flags)) {
	    warn("Upstream sent invalid init");
	    goto disconnect;
	}

	/* record the flags, and log the reconnection, set TCP_NODELAY */
	proxy_finish_connect_to_upstream(proxy, upstream_size,
					 upstream_flags);

	/* Currently, we only get disconnected from upstream (so needing to come
	 * here) when we have an outstanding request. If that becomes false,
	 * we'll need to choose the right state to return to here */
	proxy->init.needle = 0;
	return WRITE_TO_UPSTREAM;
    }

    return state;

  disconnect:
    proxy->init.needle = 0;
    proxy->init.size = 0;
    return CONNECT_TO_UPSTREAM;
}

int proxy_write_to_upstream(struct proxier *proxy, int state)
{
    ssize_t count;

//      assert( state == WRITE_TO_UPSTREAM );

    /* FIXME: We may set cork=1 multiple times as a result of this idiom.
     * Not a serious problem, but we could do better
     */
    if (proxy->req.needle == 0 && AF_UNIX != proxy->connect_to.family) {
	if (sock_set_tcp_cork(proxy->upstream_fd, 1) == -1) {
	    warn(SHOW_ERRNO("Failed to set TCP_CORK"));
	}
    }

    count = iobuf_write(proxy->upstream_fd, &proxy->req);

    if (count == -1) {
	warn(SHOW_ERRNO("Failed to send request to upstream"));
	proxy->req.needle = 0;
	// We're throwing the socket away so no need to uncork
	return CONNECT_TO_UPSTREAM;
    }

    if (proxy->req.needle == proxy->req.size) {
	/* Request sent. Advance to reading the response from upstream. We might
	 * still need req.size if reading the reply fails - we disconnect
	 * and resend the reply in that case - so keep it around for now. */
	proxy->req.needle = 0;

	if (AF_UNIX != proxy->connect_to.family) {
	    if (sock_set_tcp_cork(proxy->upstream_fd, 0) == -1) {
		warn(SHOW_ERRNO("Failed to unset TCP_CORK"));
		// TODO: should we return to CONNECT_TO_UPSTREAM in this instance?
	    }
	}

	return READ_FROM_UPSTREAM;
    }

    return state;
}

int proxy_read_from_upstream(struct proxier *proxy, int state)
{
    ssize_t count;

    struct nbd_reply *reply = &(proxy->rsp_hdr);
    struct nbd_reply_raw *reply_raw =
	(struct nbd_reply_raw *) proxy->rsp.buf;

    /* We can't assume the NBD_REPLY_SIZE + req->len is what we'll get back */
    count = iobuf_read(proxy->upstream_fd, &proxy->rsp, NBD_REPLY_SIZE);

    if (count == -1) {
	warn(SHOW_ERRNO("Failed to get reply from upstream"));
	goto disconnect;
    }

    if (proxy->rsp.needle == NBD_REPLY_SIZE) {
	nbd_r2h_reply(reply_raw, reply);

	if (reply->magic != REPLY_MAGIC) {
	    warn("Reply magic is incorrect");
	    goto disconnect;
	}

	if (proxy->req_hdr.type == REQUEST_READ) {
	    /* Get the read reply data too. */
	    proxy->rsp.size += proxy->req_hdr.len;
	}
    }

    if (proxy->rsp.size == proxy->rsp.needle) {
	debug("NBD reply received from upstream.");
	proxy->rsp.needle = 0;
	return WRITE_TO_DOWNSTREAM;
    }

    return state;

  disconnect:
    proxy->rsp.needle = 0;
    proxy->rsp.size = 0;
    return CONNECT_TO_UPSTREAM;
}


int proxy_write_to_downstream(struct proxier *proxy, int state)
{
    ssize_t count;

//      assert( state == WRITE_TO_DOWNSTREAM );

    if (!proxy->hello_sent) {
	info("Writing init to downstream");
    }

    count = iobuf_write(proxy->downstream_fd, &proxy->rsp);

    if (count == -1) {
	warn(SHOW_ERRNO("Failed to write to downstream"));
	return EXIT;
    }

    if (proxy->rsp.needle == proxy->rsp.size) {
	if (!proxy->hello_sent) {
	    info("Hello message sent to client");
	    proxy->hello_sent = 1;
	} else {
	    debug("Reply sent");
	    proxy->req_count++;
	}

	/* We're done with the request & response buffers now */
	proxy->req.size = 0;
	proxy->req.needle = 0;
	proxy->rsp.size = 0;
	proxy->rsp.needle = 0;
	return READ_FROM_DOWNSTREAM;
    }

    return state;
}

/* Non-blocking proxy session. Simple(ish) state machine. We read from d/s until
 * we have a full request, then try to write that request u/s. If writing fails,
 * we reconnect to upstream and retry. Once we've successfully written, we
 * attempt to read the reply. If that fails or times out (we give it 30 seconds)
 * then we disconnect from u/s and go back to trying to reconnect and resend.
 *
 * This is the second-simplest NBD proxy I can think of. The first version was
 * non-blocking I/O, but it was getting impossible to manage exceptional stuff
 */
void proxy_session(struct proxier *proxy)
{
    uint64_t state_started = monotonic_time_ms();
    int old_state = EXIT;
    int state;
    int connect_to_upstream_cooldown = 0;


    /* First action: Write hello to downstream */
    nbd_hello_to_buf((struct nbd_init_raw *) proxy->rsp.buf,
		     proxy->upstream_size, proxy->upstream_flags);
    proxy->rsp.size = sizeof(struct nbd_init_raw);
    proxy->rsp.needle = 0;
    state = WRITE_TO_DOWNSTREAM;


    info("Beginning proxy session on fd %i", proxy->downstream_fd);

    while (state != EXIT) {

	struct timeval select_timeout = {
	    .tv_sec = 0,
	    .tv_usec = 0
	};

	struct timeval *select_timeout_ptr = NULL;

	int result;		/* used by select() */

	fd_set rfds;
	fd_set wfds;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);

	if (state != old_state) {
	    state_started = monotonic_time_ms();

	    debug("State transition from %s to %s",
		  proxy_session_state_names[old_state],
		  proxy_session_state_names[state]
		);
	} else {
	    debug("Proxy is in state %s", proxy_session_state_names[state],
		  state);
	}

	old_state = state;

	switch (state) {
	case READ_FROM_DOWNSTREAM:
	    FD_SET(proxy->downstream_fd, &rfds);
	    break;
	case WRITE_TO_DOWNSTREAM:
	    FD_SET(proxy->downstream_fd, &wfds);
	    break;
	case WRITE_TO_UPSTREAM:
	    select_timeout.tv_sec = 15;
	    FD_SET(proxy->upstream_fd, &wfds);
	    break;
	case CONNECT_TO_UPSTREAM:
	    /* upstream_fd is now -1 */
	    proxy_disconnect_from_upstream(proxy);

	    if (connect_to_upstream_cooldown) {
		connect_to_upstream_cooldown = 0;
		select_timeout.tv_sec = 3;
	    } else {
		proxy_start_connect_to_upstream(proxy);

		if (proxy->upstream_fd == -1) {
		    warn(SHOW_ERRNO("Error acquiring socket to upstream"));
		    continue;
		}
		FD_SET(proxy->upstream_fd, &wfds);
		select_timeout.tv_sec = 15;
	    }
	    break;
	case READ_INIT_FROM_UPSTREAM:
	case READ_FROM_UPSTREAM:
	    select_timeout.tv_sec = 15;
	    FD_SET(proxy->upstream_fd, &rfds);
	    break;
	};

	if (select_timeout.tv_sec > 0) {
	    select_timeout_ptr = &select_timeout;
	}

	result =
	    sock_try_select(FD_SETSIZE, &rfds, &wfds, NULL,
			    select_timeout_ptr);

	if (result == -1) {
	    warn(SHOW_ERRNO("select() failed: "));
	    break;
	}

	/* Happens after failed reconnect. Avoid SIGBUS on FD_ISSET() */
	if (proxy->upstream_fd == -1) {
	    continue;
	}

	switch (state) {
	case READ_FROM_DOWNSTREAM:
	    if (FD_ISSET(proxy->downstream_fd, &rfds)) {
		state = proxy_read_from_downstream(proxy, state);
		/* Check if we can fulfil the request from prefetch, or
		 * rewrite the request to fill the prefetch buffer if needed
		 */
		if (proxy_prefetches(proxy) && state == WRITE_TO_UPSTREAM) {
		    state = proxy_prefetch_for_request(proxy, state);
		}
	    }
	    break;
	case CONNECT_TO_UPSTREAM:
	    if (FD_ISSET(proxy->upstream_fd, &wfds)) {
		state =
		    proxy_continue_connecting_to_upstream(proxy, state);
	    }
	    /* Leaving state untouched will retry connecting to upstream -
	     * so introduce a bit of sleep  */
	    if (state == CONNECT_TO_UPSTREAM) {
		connect_to_upstream_cooldown = 1;
	    }

	    break;
	case READ_INIT_FROM_UPSTREAM:
	    state = proxy_read_init_from_upstream(proxy, state);

	    if (state == CONNECT_TO_UPSTREAM) {
		connect_to_upstream_cooldown = 1;
	    }

	    break;
	case WRITE_TO_UPSTREAM:
	    if (FD_ISSET(proxy->upstream_fd, &wfds)) {
		state = proxy_write_to_upstream(proxy, state);
	    }
	    break;
	case READ_FROM_UPSTREAM:
	    if (FD_ISSET(proxy->upstream_fd, &rfds)) {
		state = proxy_read_from_upstream(proxy, state);
	    }
	    /* Fill the prefetch buffer and rewrite the reply, if needed */
	    if (proxy_prefetches(proxy) && state == WRITE_TO_DOWNSTREAM) {
		state = proxy_prefetch_for_reply(proxy, state);
	    }
	    break;
	case WRITE_TO_DOWNSTREAM:
	    if (FD_ISSET(proxy->downstream_fd, &wfds)) {
		state = proxy_write_to_downstream(proxy, state);
	    }
	    break;
	}

	/* In these states, we're interested in restarting after a timeout.
	 */
	if (old_state == state && proxy_state_upstream(state)) {
	    if ((monotonic_time_ms()) - state_started > UPSTREAM_TIMEOUT) {
		warn("Timed out in state %s while communicating with upstream", proxy_session_state_names[state]
		    );
		state = CONNECT_TO_UPSTREAM;

		/* Since we've timed out, we won't have gone through the timeout logic
		 * in the various state handlers that resets these appropriately... */
		proxy->init.size = 0;
		proxy->init.needle = 0;
		proxy->rsp.size = 0;
		proxy->rsp.needle = 0;
		proxy->req.size = 0;
		proxy->req.needle = 0;
	    }
	}
    }

    info("Finished proxy session on fd %i after %" PRIu64
	 " successful request(s)", proxy->downstream_fd, proxy->req_count);

    /* Reset these two for the next session */
    proxy->req_count = 0;
    proxy->hello_sent = 0;

    return;
}

/** Accept an NBD socket connection, dispatch appropriately */
int proxy_accept(struct proxier *params)
{
    NULLCHECK(params);

    int client_fd;
    fd_set fds;

    union mysockaddr client_address;
    socklen_t socklen = sizeof(client_address);

    info("Waiting for client connection");

    FD_ZERO(&fds);
    FD_SET(params->listen_fd, &fds);

    FATAL_IF_NEGATIVE(sock_try_select(FD_SETSIZE, &fds, NULL, NULL, NULL),
		      SHOW_ERRNO("select() failed")
	);

    if (FD_ISSET(params->listen_fd, &fds)) {
	client_fd =
	    accept(params->listen_fd, &client_address.generic, &socklen);

	if (client_address.family != AF_UNIX) {
	    if (sock_set_tcp_nodelay(client_fd, 1) == -1) {
		warn(SHOW_ERRNO("Failed to set TCP_NODELAY"));
	    }
	}

	info("Accepted nbd client socket fd %d", client_fd);
	sock_set_nonblock(client_fd, 1);
	params->downstream_fd = client_fd;
	proxy_session(params);

	WARN_IF_NEGATIVE(sock_try_close(params->downstream_fd),
			 "Couldn't close() downstram fd %i after proxy session",
			 params->downstream_fd);
	params->downstream_fd = -1;
    }

    return 1;			/* We actually expect to be interrupted by signal handlers */
}


void proxy_accept_loop(struct proxier *params)
{
    NULLCHECK(params);
    while (proxy_accept(params));
}

/** Closes sockets */
void proxy_cleanup(struct proxier *proxy)
{
    NULLCHECK(proxy);

    info("Cleaning up");

    if (-1 != proxy->listen_fd) {

	if (AF_UNIX == proxy->listen_on.family) {
	    if (-1 == unlink(proxy->listen_on.un.sun_path)) {
		warn(SHOW_ERRNO
		     ("Failed to unlink %s",
		      proxy->listen_on.un.sun_path));
	    }
	}

	WARN_IF_NEGATIVE(sock_try_close(proxy->listen_fd),
			 SHOW_ERRNO("Failed to close() listen fd %i",
				    proxy->listen_fd)
	    );
	proxy->listen_fd = -1;
    }

    if (-1 != proxy->downstream_fd) {
	WARN_IF_NEGATIVE(sock_try_close(proxy->downstream_fd),
			 SHOW_ERRNO("Failed to close() downstream fd %i",
				    proxy->downstream_fd)
	    );
	proxy->downstream_fd = -1;
    }

    if (-1 != proxy->upstream_fd) {
	WARN_IF_NEGATIVE(sock_try_close(proxy->upstream_fd),
			 SHOW_ERRNO("Failed to close() upstream fd %i",
				    proxy->upstream_fd)
	    );
	proxy->upstream_fd = -1;
    }

    info("Cleanup done");
}

/** Full lifecycle of the proxier */
int do_proxy(struct proxier *params)
{
    NULLCHECK(params);

    info("Ensuring upstream server is open");

    if (!proxy_connect_to_upstream(params)) {
	warn("Couldn't connect to upstream server during initialization, exiting");
	proxy_cleanup(params);
	return 1;
    };

    proxy_open_listen_socket(params);
    proxy_accept_loop(params);
    proxy_cleanup(params);

    return 0;
}
