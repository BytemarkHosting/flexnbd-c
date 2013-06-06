#include "proxy.h"
#include "readwrite.h"

#ifdef PREFETCH
#include "prefetch.h"
#endif


#include "ioutil.h"
#include "sockutil.h"
#include "util.h"

#include <errno.h>

#include <sys/socket.h>
#include <netinet/tcp.h>

struct proxier* proxy_create(
	char* s_downstream_address,
	char* s_downstream_port,
	char* s_upstream_address,
	char* s_upstream_port,
	char* s_upstream_bind )
{
	struct proxier* out;
	out = xmalloc( sizeof( struct proxier ) );

	FATAL_IF_NULL(s_downstream_address, "Listen address not specified");
	NULLCHECK( s_downstream_address );

	FATAL_UNLESS(
		parse_to_sockaddr( &out->listen_on.generic, s_downstream_address ),
		"Couldn't parse downstream address %s"
	);

	if ( out->listen_on.family != AF_UNIX ) {
		FATAL_IF_NULL( s_downstream_port, "Downstream port not specified" );
		NULLCHECK( s_downstream_port );
		parse_port( s_downstream_port, &out->listen_on.v4 );
	}

	FATAL_IF_NULL(s_upstream_address, "Upstream address not specified");
	NULLCHECK( s_upstream_address );

	FATAL_UNLESS(
		parse_ip_to_sockaddr( &out->connect_to.generic, s_upstream_address ),
		"Couldn't parse upstream address '%s'",
		s_upstream_address
	);

	FATAL_IF_NULL( s_upstream_port, "Upstream port not specified" );
	NULLCHECK( s_upstream_port );
	parse_port( s_upstream_port, &out->connect_to.v4 );

	if ( s_upstream_bind ) {
		FATAL_IF_ZERO(
			parse_ip_to_sockaddr( &out->connect_from.generic, s_upstream_bind ),
			"Couldn't parse bind address '%s'",
			s_upstream_bind
		);
		out->bind = 1;
	}

	out->listen_fd = -1;
	out->downstream_fd = -1;
	out->upstream_fd = -1;

#ifdef PREFETCH
	out->prefetch = xmalloc( sizeof( struct prefetch ) );
#endif


	out->req_buf = xmalloc( NBD_MAX_SIZE );
	out->rsp_buf = xmalloc( NBD_MAX_SIZE );

	return out;
}

void proxy_destroy( struct proxier* proxy )
{
	free( proxy->req_buf );
	free( proxy->rsp_buf );
#ifdef PREFETCH
	free( proxy->prefetch );
#endif

	free( proxy );
}


/* Try to establish a connection to our upstream server. Return 1 on success,
 * 0 on failure
 */
int proxy_connect_to_upstream( struct proxier* proxy )
{
	struct sockaddr* connect_from = NULL;
	if ( proxy->bind ) {
		connect_from = &proxy->connect_from.generic;
	}

	int fd = socket_connect( &proxy->connect_to.generic, connect_from );
	off64_t size = 0;

	if ( -1 == fd ) {
		return 0;
	}

	if( !socket_nbd_read_hello( fd, &size ) ) {
		WARN_IF_NEGATIVE(
			sock_try_close( fd ),
			"Couldn't close() after failed read of NBD hello on fd %i", fd
		);
		return 0;
	}

	if ( proxy->upstream_size == 0 ) {
		info( "Size of upstream image is %"PRIu64" bytes", size );
	} else if ( proxy->upstream_size != size ) {
		warn( "Size changed from %"PRIu64" to %"PRIu64" bytes", proxy->upstream_size, size );
	}

	proxy->upstream_size = size;
	proxy->upstream_fd = fd;

	return 1;
}

void proxy_disconnect_from_upstream( struct proxier* proxy )
{
	if ( -1 != proxy->upstream_fd ) {
		info(" Closing upstream connection" );

		/* TODO: An NBD disconnect would be pleasant here */
		WARN_IF_NEGATIVE(
			sock_try_close( proxy->upstream_fd ),
			"Failed to close() fd %i when disconnecting from upstream",
			proxy->upstream_fd
		);
		proxy->upstream_fd = -1;
	}
}


/** Prepares a listening socket for the NBD server, binding etc. */
void proxy_open_listen_socket(struct proxier* params)
{
	NULLCHECK( params );

	params->listen_fd = socket(params->listen_on.family, SOCK_STREAM, 0);
	FATAL_IF_NEGATIVE(
		params->listen_fd, SHOW_ERRNO( "Couldn't create listen socket" )
	);

	/* Allow us to restart quickly */
	FATAL_IF_NEGATIVE(
		sock_set_reuseaddr(params->listen_fd, 1),
		SHOW_ERRNO( "Couldn't set SO_REUSEADDR" )
	);

	if( AF_UNIX != params->listen_on.family ) {
		FATAL_IF_NEGATIVE(
			sock_set_tcp_nodelay(params->listen_fd, 1),
			SHOW_ERRNO( "Couldn't set TCP_NODELAY" )
		);
	}

	FATAL_UNLESS_ZERO(
		sock_try_bind( params->listen_fd, &params->listen_on.generic ),
		SHOW_ERRNO( "Failed to bind to listening socket" )
	);

	/* We're only serving one client at a time, hence backlog of 1 */
	FATAL_IF_NEGATIVE(
		listen(params->listen_fd, 1),
		SHOW_ERRNO( "Failed to listen on listening socket" )
	);

	info( "Now listening for incoming connections" );
}

/* Try to get a request from downstream. If reading from downstream fails, then
 * the session will be over. Returns 1 on success, 0 on failure.
 */
int proxy_get_request_from_downstream( struct proxier* proxy )
{
	unsigned char* req_hdr_raw = proxy->req_buf;
	unsigned char* req_data = proxy->req_buf + NBD_REQUEST_SIZE;
	size_t req_buf_size;

	struct nbd_request_raw* request_raw = (struct nbd_request_raw*) req_hdr_raw;
	struct nbd_request*     request = &(proxy->req_hdr);

	if ( readloop( proxy->downstream_fd, req_hdr_raw, NBD_REQUEST_SIZE ) == -1 ) {
		info( SHOW_ERRNO( "Failed to get request header from downstream" ) );
		return 0;
	}

	nbd_r2h_request( request_raw, request );
	req_buf_size = NBD_REQUEST_SIZE;

	if ( request->type == REQUEST_DISCONNECT ) {
		info( "Received disconnect request from client" );
		return 0;
	}

	if ( request->type == REQUEST_READ ) {
		if (request->len > ( NBD_MAX_SIZE - NBD_REPLY_SIZE ) ) {
			warn( "NBD read request size %"PRIu32" too large", request->len );
			return 0;
		}

	}

	if ( request->type == REQUEST_WRITE ) {
		if (request->len > ( NBD_MAX_SIZE - NBD_REQUEST_SIZE ) ) {
			warn( "NBD write request size %"PRIu32" too large", request->len );
			return 0;
		}

		if ( readloop( proxy->downstream_fd, req_data, request->len ) == -1 ) {
			warn( "Failed to get NBD write request data: %"PRIu32"b", request->len );
			return 0;
		}

		req_buf_size += request->len;
	}

	debug(
		"Received NBD request from downstream. type=%"PRIu32" from=%"PRIu64" len=%"PRIu32,
		request->type, request->from, request->len
	);

	proxy->req_buf_size = req_buf_size;
	return 1;
}

/* Tries to send the request upstream and receive a response. If upstream breaks
 * then we reconnect to it, and keep it up until we have a complete response
 * back. Returns 1 on success, 0 on failure
 */
int proxy_run_request_upstream( struct proxier* proxy )
{
	unsigned char* rsp_hdr_raw  = proxy->rsp_buf;
	unsigned char* rsp_data = proxy->rsp_buf + NBD_REPLY_SIZE;

	struct nbd_reply_raw* reply_raw = (struct nbd_reply_raw*) rsp_hdr_raw;
	struct nbd_request*   request   = &(proxy->req_hdr);

#ifdef PREFETCH
	/* We only want to consider prefetching if we know we're not
	 * getting too much data back, if it's a read request, and if
	 * the prefetch won't try to read past the end of the file.
	 */
	int prefetching =
		request->len <= PREFETCH_BUFSIZE &&
		request->type == REQUEST_READ &&
		(request->from + request->len * 2) <= proxy->upstream_size;

	/* We pull the request out of the proxy struct, rewrite the
	 * request size, and write it back.
	 */
	if ( prefetching ) {
		/* We need a malloc here because nbd_h2r_request craps
		 * out if passed an address on the stack
		 */
		struct nbd_request* rewrite_request =
			xmalloc( sizeof( struct nbd_request ) );
		NULLCHECK( rewrite_request );
		memcpy( rewrite_request,
			request,
			sizeof( struct nbd_request ) );

		rewrite_request->len *= 2;
		debug( "Prefetching %d bytes", rewrite_request->len );

		nbd_h2r_request( rewrite_request,
				 (struct nbd_request_raw *)proxy->req_buf );
		free( rewrite_request );
	}
#endif

	struct nbd_reply* reply = &(proxy->rsp_hdr);

	size_t rsp_buf_size;

#ifdef PREFETCH
	if ( request->type == REQUEST_READ ){
		/* See if we can respond with what's in our prefetch
		 * cache
		 */
		if( proxy->prefetch->is_full &&
		    request->from == proxy->prefetch->from &&
		    request->len  == proxy->prefetch->len ) {
			/* HUZZAH!  A match! */
			debug( "Prefetch hit!" );

			/* First build a reply header */
			struct nbd_reply prefetch_reply =
				{REPLY_MAGIC, 0, "01234567"};
			memcpy( &(prefetch_reply.handle), request->handle, 8 );

			/* now copy it into the response */
			nbd_h2r_reply( &prefetch_reply, reply_raw );

			/* and the data */
			memcpy( rsp_data,
				proxy->prefetch->buffer,
				proxy->prefetch->len );

			proxy->rsp_buf_size =
				NBD_REPLY_SIZE + proxy->prefetch->len;

			/* return early, our work here is done */
			return 1;
		}
	}
	else {
		/* Safety catch.  If we're sending a write request, we
		 * blow away the cache.  This is very pessimistic, but
		 * it's simpler (and therefore safer) than working out
		 * whether we can keep it or not.
		 */
		debug("Blowing away prefetch cache on type %d request.",
		      request->type);
		proxy->prefetch->is_full = 0;
	}
#endif

	if ( -1 == writeloop( proxy->upstream_fd,
			      proxy->req_buf,
			      proxy->req_buf_size ) ) {
		warn( SHOW_ERRNO( "Failed to send request to upstream" ) );
		goto disconnect;
	}

	if ( -1 == readloop( proxy->upstream_fd,
			     rsp_hdr_raw,
			     NBD_REPLY_SIZE ) ) {
		warn( SHOW_ERRNO( "Failed to get reply header from upstream" ) );
		goto disconnect;
	}

	nbd_r2h_reply( reply_raw, reply );
	rsp_buf_size = NBD_REPLY_SIZE;

	if ( reply->magic != REPLY_MAGIC ) {
		debug( "Reply magic is incorrect" );
		goto disconnect;
	}

	debug( "NBD reply received from upstream. Response code: %"PRIu32,
	       reply->error );

	if ( reply->error != 0 ) {
		warn( "NBD  error returned from upstream: %"PRIu32,
		      reply->error );
	}

	if ( reply->error == 0 && request->type == REQUEST_READ ) {
#ifdef PREFETCH
		if ( -1 == readloop( proxy->upstream_fd,
				    rsp_data,
				    request->len ) ) {
			warn( SHOW_ERRNO( "Failed to get read reply data from upstream" ) );
			goto disconnect;
		}

		if ( prefetching ) {
			if ( -1 == readloop( proxy->upstream_fd,
					    &(proxy->prefetch->buffer),
					    request->len ) ) {
				warn( SHOW_ERRNO( "Failed to get prefetch read reply data from upstream" ) );
				goto disconnect;
			}
			proxy->prefetch->from = request->from + request->len;
			proxy->prefetch->len  = request->len;
			proxy->prefetch->is_full = 1;
		}
#else

		if ( -1 == readloop( proxy->upstream_fd,
				     rsp_data,
				     request->len ) ) {
			warn( SHOW_ERRNO( "Failed to get read reply data from upstream" ) );
			goto disconnect;
		}
#endif
		rsp_buf_size += request->len;
	}

	proxy->rsp_buf_size = rsp_buf_size;
	return 1;

disconnect:
	proxy_disconnect_from_upstream( proxy );
#ifdef PREFETCH
	proxy->prefetch->is_full = 0;
#endif
	return 0;
}

/* Write an NBD reply back downstream. Return 0 on failure, 1 on success. */
int proxy_send_reply_downstream( struct proxier* proxy )
{
	int result;
	unsigned char* rsp_buf = proxy->rsp_buf;

	debug(
		"Writing header (%"PRIu32") + data (%"PRIu32") bytes downstream",
		NBD_REPLY_SIZE, proxy->rsp_buf_size - NBD_REPLY_SIZE
	);

	result = writeloop( proxy->downstream_fd, rsp_buf, proxy->rsp_buf_size );
	if ( result == -1 ) {
		warn( SHOW_ERRNO( "Failed to send reply downstream" ) );
		return 0;
	}

	debug( "Reply sent" );
	return 1;
}


/* Here, we negotiate an NBD session with downstream, based on the information
 * we got on first connection to upstream. Then we wait for a request to come
 * in from downstream, read it into memory, then send it to upstream. If
 * upstream dies before responding, we reconnect to upstream and resend it.
 * Once we've got a response, we write it directly to downstream, and wait for a
 * new request. When downstream disconnects, or we receive an exit signal (which
 * can be blocked, unfortunately), we are finished.
 *
 * This is the simplest possible nbd proxy I can think of. It may not be at all
 * performant - let's see.
 */

void proxy_session( struct proxier* proxy )
{
    int downstream_fd = proxy->downstream_fd;
	uint64_t req_count = 0;
	int result;

	info( "Beginning proxy session on fd %i", downstream_fd );

	if ( !socket_nbd_write_hello( downstream_fd, proxy->upstream_size ) ) {
		warn( "Sending hello failed on fd %i, ending session", downstream_fd );
		return;
	}

	while( proxy_get_request_from_downstream( proxy ) ) {
		do {
			if ( proxy->upstream_fd == -1 ) {
				info( "Connecting to upstream" );
				if ( !proxy_connect_to_upstream( proxy ) ) {
					warn( "Failed to connect to upstream" );
					result = 0;
					sleep( 5 );
					continue;
				}
				info( "Connected to upstream");
			}
			result = proxy_run_request_upstream( proxy );
		} while ( result == 0 );

		if ( !proxy_send_reply_downstream( proxy ) ) {
			warn( "Replying on fd %i failed, ending session", downstream_fd );
			break;
		}

		proxy->req_buf_size = 0;
		proxy->rsp_buf_size = 0;

		req_count++;
	};

	info(
		"Finished proxy session on fd %i after %"PRIu64" successful request(s)",
		downstream_fd, req_count
	);

	return;
}

/** Accept an NBD socket connection, dispatch appropriately */
int proxy_accept( struct proxier* params )
{
	NULLCHECK( params );

	int              client_fd;
	fd_set           fds;

	union mysockaddr client_address;
	socklen_t        socklen = sizeof( client_address );

	info( "Waiting for client connection" );

	FD_ZERO(&fds);
	FD_SET(params->listen_fd, &fds);

	FATAL_IF_NEGATIVE(
		sock_try_select(FD_SETSIZE, &fds, NULL, NULL, NULL),
		SHOW_ERRNO( "select() failed" )
	);

	if ( FD_ISSET( params->listen_fd, &fds ) ) {
		client_fd = accept( params->listen_fd, &client_address.generic, &socklen );

		if ( client_address.family != AF_UNIX ) {
			if ( sock_set_tcp_nodelay(client_fd, 1) == -1 ) {
				warn( SHOW_ERRNO( "Failed to set TCP_NODELAY" ) );
			}
		}

		info( "Accepted nbd client socket fd %d", client_fd );
		params->downstream_fd = client_fd;
		proxy_session( params );

		WARN_IF_NEGATIVE(
			sock_try_close( params->downstream_fd ),
			"Couldn't close() downstram fd %i after proxy session",
			params->downstream_fd
		);
		params->downstream_fd = -1;
	}

	return 1; /* We actually expect to be interrupted by signal handlers */
}


void proxy_accept_loop( struct proxier* params )
{
	NULLCHECK( params );
	while( proxy_accept( params ) );
}

/** Closes sockets */
void proxy_cleanup( struct proxier* proxy )
{
	NULLCHECK( proxy );

	info( "Cleaning up" );

	if ( -1 != proxy->listen_fd ) {
		WARN_IF_NEGATIVE(
			sock_try_close( proxy->listen_fd ),
			SHOW_ERRNO( "Failed to close() listen fd %i", proxy->listen_fd )
		);
		proxy->listen_fd = -1;
	}

	if ( -1 != proxy->downstream_fd ) {
		WARN_IF_NEGATIVE(
			sock_try_close( proxy->downstream_fd ),
			SHOW_ERRNO(
				"Failed to close() downstream fd %i", proxy->downstream_fd
			)
		);
		proxy->downstream_fd = -1;
	}

	if ( -1 != proxy->upstream_fd ) {
		WARN_IF_NEGATIVE(
			sock_try_close( proxy->upstream_fd ),
			SHOW_ERRNO(
				"Failed to close() upstream fd %i", proxy->upstream_fd
			)
		);
		proxy->upstream_fd = -1;
	}

	if ( AF_UNIX == proxy->listen_on.family ) {
		if ( -1 == unlink( proxy->listen_on.un.sun_path ) ) {
			warn( SHOW_ERRNO( "Failed to unlink %s", proxy->listen_on.un.sun_path ) );
		}
	}

	info( "Cleanup done" );
}

/** Full lifecycle of the proxier */
int do_proxy( struct proxier* params )
{
	NULLCHECK( params );

	info( "Ensuring upstream server is open" );

	if ( !proxy_connect_to_upstream( params ) ) {
		warn( "Couldn't connect to upstream server during initialization, exiting" );
		proxy_cleanup( params );
		return 1;
	};

	proxy_open_listen_socket( params );
	proxy_accept_loop( params );
	proxy_cleanup( params );

	return 0;
}

