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

/* Shared between our two different connect_to_upstream paths */
void proxy_finish_connect_to_upstream( struct proxier *proxy, off64_t size );

/* Try to establish a connection to our upstream server. Return 1 on success,
 * 0 on failure. this is a blocking call that returns a non-blocking socket.
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

	proxy_finish_connect_to_upstream( proxy, size );
	proxy->upstream_fd = fd;
	sock_set_nonblock( fd, 1 );

	return 1;
}

/* First half of non-blocking connection to upstream. Gets as far as calling
 * connect() on a non-blocking socket.
 */
void proxy_start_connect_to_upstream( struct proxier* proxy )
{
	int fd, result;
	struct sockaddr* from = NULL;
	struct sockaddr* to = &proxy->connect_to.generic;

	if ( proxy->bind ) {
		from = &proxy->connect_from.generic;
	}

	fd = socket( to->sa_family , SOCK_STREAM, 0 );

	if( fd < 0 ) {
		warn( SHOW_ERRNO( "Couldn't create socket to reconnect to upstream" ) );
		return;
	}

	info( "Beginning non-blocking connection to upstream on fd %i", fd );

	if ( NULL != from ) {
		if ( 0 > bind( fd, from, sockaddr_size( from ) ) ) {
			warn( SHOW_ERRNO( "bind() to source address failed" ) );
		}
	}

	result = sock_set_nonblock( fd, 1 );
	if ( result == -1 ) {
		warn( SHOW_ERRNO( "Failed to set upstream fd %i non-blocking", fd ) );
		goto error;
	}

	result = connect( fd, to, sockaddr_size( to ) );
	if ( result == -1 && errno != EINPROGRESS ) {
		warn( SHOW_ERRNO( "Failed to start connect()ing to upstream!" ) );
		goto error;
	}

	proxy->upstream_fd = fd;
	return;

error:
	if ( sock_try_close( fd ) == -1 ) {
		/* Non-fatal leak, although still nasty */
		warn( SHOW_ERRNO( "Failed to close fd for upstream %i", fd ) );
	}
	return;
}

void proxy_finish_connect_to_upstream( struct proxier *proxy, off64_t size ) {

	if ( proxy->upstream_size == 0 ) {
		info( "Size of upstream image is %"PRIu64" bytes", size );
	} else if ( proxy->upstream_size != size ) {
		warn(
			"Size changed from %"PRIu64" to %"PRIu64" bytes",
			proxy->upstream_size, size
		);
	}

	proxy->upstream_size = size;
	info( "Connected to upstream on fd %i", proxy->upstream_fd );

	return;
}

void proxy_disconnect_from_upstream( struct proxier* proxy )
{
	if ( -1 != proxy->upstream_fd ) {
		info("Closing upstream connection on fd %i", proxy->upstream_fd );

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

	return;
}


#ifdef PREFETCH

int proxy_prefetch_for_request( struct proxier* proxy, int state )
{
	/* TODO: restore prefetch capability */
	return state;

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
//####
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
}

int proxy_prefetch_for_reply( struct proxier* proxy, int state )
{
	return state;

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

}

#endif

typedef enum {
	EXIT,
	WRITE_TO_DOWNSTREAM,
	READ_FROM_DOWNSTREAM,
	CONNECT_TO_UPSTREAM,
	READ_INIT_FROM_UPSTREAM,
	WRITE_TO_UPSTREAM,
	READ_FROM_UPSTREAM
} proxy_session_states;

static char* proxy_session_state_names[] = {
  "EXIT",
  "WRITE_TO_DOWNSTREAM",
  "READ_FROM_DOWNSTREAM",
  "CONNECT_TO_UPSTREAM",
  "READ_INIT_FROM_UPSTREAM",
  "WRITE_TO_UPSTREAM",
  "READ_FROM_UPSTREAM"
};

static inline int io_errno_permanent()
{
	return ( errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR );
}


int proxy_read_from_downstream( struct proxier *proxy, int state )
{
	ssize_t count;

	struct nbd_request_raw* request_raw = (struct nbd_request_raw*) proxy->req_buf;
	struct nbd_request*     request = &(proxy->req_hdr);

//	assert( state == READ_FROM_DOWNSTREAM );

	/* We're beginning a new read from downstream */
	if ( proxy->req_buf_size == 0 ) {
		proxy->req_buf_size = NBD_REQUEST_SIZE;
	}

read:
	debug(
		"Reading %"PRIu32" of %"PRIu32" bytes from downstream for NBD request",
		proxy->req_buf_size - proxy->req_buf_offset,
		proxy->req_buf_size
	);

	count = read(
		proxy->downstream_fd,
		proxy->req_buf + proxy->req_buf_offset,
		proxy->req_buf_size - proxy->req_buf_offset
	);

	if ( count == 0 ) {
		info( "EOF on downstream fd %i received", proxy->downstream_fd );
		return EXIT;
	}

	if ( count != -1 ) {
		proxy->req_buf_offset += count;
	} else if ( io_errno_permanent() ) {
		warn( SHOW_ERRNO( "Couldn't read request from downstream: " ) );
		return EXIT;
	}

	if ( proxy->req_buf_offset == NBD_REQUEST_SIZE ) {
		nbd_r2h_request( request_raw, request );

		if ( request->type == REQUEST_DISCONNECT ) {
			info( "Received disconnect request from client" );
			return EXIT;
		}

		/* Simple validations */
		if ( request->type == REQUEST_READ ) {
			if (request->len > ( NBD_MAX_SIZE - NBD_REPLY_SIZE ) ) {
				warn( "NBD read request size %"PRIu32" too large", request->len );
				return EXIT;
			}
		}
		if ( request->type == REQUEST_WRITE ) {
			if (request->len > ( NBD_MAX_SIZE - NBD_REQUEST_SIZE ) ) {
				warn( "NBD write request size %"PRIu32" too large", request->len );
				return EXIT;
			}

			proxy->req_buf_size += request->len;

			/* Minor optimisation: Read again immediately if there's write
			 * request data to be had. No point select()ing again
			 */
			goto read;
		}
	}

	if ( proxy->req_buf_offset == proxy->req_buf_size ) {
		debug(
			"Received NBD request from downstream. type=%"PRIu32" from=%"PRIu64" len=%"PRIu32,
			request->type, request->from, request->len
		);

		/* Finished reading, so advance state. Leave size untouched so the next
		 * state knows how many bytes to write */
		proxy->req_buf_offset = 0;
		return WRITE_TO_UPSTREAM;
	}

	return state;
}

int proxy_continue_connecting_to_upstream( struct proxier* proxy, int state )
{
	int error, result;
	socklen_t len = sizeof( error );

//	assert( state == CONNECT_TO_UPSTREAM );

	result = getsockopt(
		proxy->upstream_fd, SOL_SOCKET, SO_ERROR,  &error, &len
	);

	if ( result == -1 ) {
		warn( SHOW_ERRNO( "Failed to tell if connected to upstream" ) );
		return state;
	}

	if ( error != 0 ) {
		errno = error;
		warn( SHOW_ERRNO( "Failed to connect to upstream" ) );
		return state;
	}

#ifdef PREFETCH
	/* Data may have changed while we were disconnected */
	proxy->prefetch->is_full = 0;
#endif

	info( "Connected to upstream on fd %i", proxy->upstream_fd );
	return READ_INIT_FROM_UPSTREAM;
}

int proxy_read_init_from_upstream( struct proxier* proxy, int state )
{
	ssize_t count;
	unsigned char* buf = (unsigned char *) &(proxy->init_buf);

//	assert( state == READ_INIT_FROM_UPSTREAM );

	debug(
		"Reading %"PRIu32" bytes of %"PRIu32" in init message",
		sizeof( proxy->init_buf ) - proxy->init_buf_offset,
		sizeof( proxy->init_buf )
	);

	count = read(
		proxy->upstream_fd,
		buf + proxy->init_buf_offset,
		sizeof( proxy->init_buf ) - proxy->init_buf_offset
	);

	if ( count == 0 ) {
		info( "EOF signalled on upstream fd %i", proxy->upstream_fd );
		goto disconnect;
	}

	if ( count != -1 ) {
		proxy->init_buf_offset += count;
	} else if ( io_errno_permanent() ) {
		warn( SHOW_ERRNO( "Failed to read init from upstream" ) );
		goto disconnect;
	}

	if ( proxy->init_buf_offset == sizeof( proxy->init_buf ) ) {
		off64_t upstream_size;
		if ( !nbd_check_hello( &proxy->init_buf, &upstream_size ) ) {
			warn( "Upstream sent invalid init" );
			goto disconnect;
		}

		/* Currently, we only get disconnected from upstream (so needing to come
		 * here) when we have an outstanding request. If that becomes false,
		 * we'll need to choose the right state to return to here */
		proxy->init_buf_offset = 0;
		return WRITE_TO_UPSTREAM;
	}

	return state;

disconnect:
	proxy->init_buf_offset = 0;
	return CONNECT_TO_UPSTREAM;
}

int proxy_write_to_upstream( struct proxier* proxy, int state )
{
	ssize_t count;

//	assert( state == WRITE_TO_UPSTREAM );

	debug(
		"Writing %"PRIu32" of %"PRIu32" bytes of request to upstream",
		proxy->req_buf_size - proxy->req_buf_offset,
		proxy->req_buf_size
	);

	count = write(
		proxy->upstream_fd,
		proxy->req_buf + proxy->req_buf_offset,
		proxy->req_buf_size - proxy->req_buf_offset
	);

	if ( count != -1 ) {
		proxy->req_buf_offset += count;
	} else if ( io_errno_permanent() ) {
		warn( SHOW_ERRNO( "Failed to send request to upstream" ) );
		proxy->req_buf_offset = 0;
		return CONNECT_TO_UPSTREAM;
	}

	if ( proxy->req_buf_offset == proxy->req_buf_size ) {
		/* Request sent. Advance to reading the response from upstream. We might
		 * still need req_buf_size if reading the reply fails - we disconnect
		 * and resend the reply in that case - so keep it around for now. */
		proxy->req_buf_offset = 0;
		return READ_FROM_UPSTREAM;
	}

	return state;
}

int proxy_read_from_upstream( struct proxier* proxy, int state )
{
	ssize_t count;

	struct nbd_reply*     reply     = &(proxy->rsp_hdr);
	struct nbd_reply_raw* reply_raw = (struct nbd_reply_raw*) proxy->rsp_buf;

	/* We can't assume the NBD_REPLY_SIZE + req->len is what we'll get back */
	if ( proxy->rsp_buf_offset == 0 ) {
		proxy->rsp_buf_size = NBD_REPLY_SIZE;
	}

read:
	debug(
		"Reading %"PRIu32" of %"PRIu32" bytes in NBD reply",
		proxy->rsp_buf_size - proxy->rsp_buf_offset,
		proxy->rsp_buf_size
	);

	count = read(
		proxy->upstream_fd,
		proxy->rsp_buf + proxy->rsp_buf_offset,
		proxy->rsp_buf_size - proxy->rsp_buf_offset
	);

	if ( count == 0 ) {
		info( "EOF signalled on upstream fd %i", proxy->upstream_fd );
		goto disconnect;
	}

	if ( count != -1 ) {
		proxy->rsp_buf_offset += count;
	} else if ( io_errno_permanent() ) {
		warn( SHOW_ERRNO( "Failed to get reply from upstream" ) );
		goto disconnect;
	}

	debug(
		"Read %"PRIu32" of %"PRIu32" bytes of reply from upstream",
		proxy->rsp_buf_offset,
		proxy->rsp_buf_size
	);

	if ( proxy->rsp_buf_offset == NBD_REPLY_SIZE ) {
		nbd_r2h_reply( reply_raw, reply );

		if ( reply->magic != REPLY_MAGIC ) {
			warn( "Reply magic is incorrect" );
			goto disconnect;
		}

		if ( reply->error != 0 ) {
			warn( "NBD error returned from upstream: %"PRIu32, reply->error );
			goto disconnect;
		}

		if ( ( proxy->req_hdr.type || REQUEST_READ ) == REQUEST_READ ) {
			/* Read the read reply data too. Same optimisation as
			 * read_from_downstream */
			proxy->rsp_buf_size += proxy->req_hdr.len;
			goto read;
		}
	}

	if ( proxy->rsp_buf_size == proxy->rsp_buf_offset ) {
		debug( "NBD reply received from upstream." );
		proxy->rsp_buf_offset = 0;
		return WRITE_TO_DOWNSTREAM;
	}

	return state;

disconnect:
	proxy->rsp_buf_offset = 0;
	proxy->rsp_buf_size = 0;
	return CONNECT_TO_UPSTREAM;
}


int proxy_write_to_downstream( struct proxier* proxy, int state )
{
	ssize_t count;

//	assert( state == WRITE_TO_DOWNSTREAM );

	if ( proxy->hello_sent ) {
		debug(
			"Writing request of %"PRIu32" bytes from offset %"PRIu32,
			proxy->rsp_buf_size, proxy->rsp_buf_offset
		);
	} else {
		debug( "Writing init to downstream" );
	}

	count = write(
		proxy->downstream_fd,
		proxy->rsp_buf + proxy->rsp_buf_offset,
		proxy->rsp_buf_size - proxy->rsp_buf_offset
	);

	if ( count != -1 ) {
		proxy->rsp_buf_offset += count;
	} else if ( io_errno_permanent() ) {
		if ( proxy->hello_sent ) {
			warn( SHOW_ERRNO( "Failed to send reply downstream" ) );
		} else {
			warn( SHOW_ERRNO( "Failed to send init downstream" ) );
		}
		return EXIT;
	}

	if ( proxy->rsp_buf_offset == proxy->rsp_buf_size ) {
		if ( !proxy->hello_sent ) {
			info( "Hello message sent to client" );
			proxy->hello_sent = 1;
		} else {
			debug( "Reply sent" );
			proxy->req_count++;
		}

		/* We're done with the request & response buffers now */
		proxy->req_buf_size = 0;
		proxy->req_buf_offset = 0;
		proxy->rsp_buf_size = 0;
		proxy->rsp_buf_offset = 0;
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
void proxy_session( struct proxier* proxy )
{
	/* First action: Write hello to downstream */
	nbd_hello_to_buf( (struct nbd_init_raw *) proxy->rsp_buf, proxy->upstream_size );
	proxy->rsp_buf_size = sizeof( struct nbd_init_raw );
	proxy->rsp_buf_offset = 0;
	int state = WRITE_TO_DOWNSTREAM;

	info( "Beginning proxy session on fd %i", proxy->downstream_fd );

	while( state != EXIT ) {

		struct timeval select_timeout = {
			.tv_sec = 0,
			.tv_usec = 0
		};

		struct timeval *select_timeout_ptr = NULL;

		int result; /* used by select() */

		fd_set rfds;
		fd_set wfds;

		FD_ZERO( &rfds );
		FD_ZERO( &wfds );

		debug( "Proxy is in state %s ( %i )", proxy_session_state_names[state], state );

		switch( state ) {
			case READ_FROM_DOWNSTREAM:
				FD_SET( proxy->downstream_fd, &rfds );
				break;
			case WRITE_TO_DOWNSTREAM:
				FD_SET( proxy->downstream_fd, &wfds );
				break;
			case WRITE_TO_UPSTREAM:
				select_timeout.tv_sec = 15;
				FD_SET(proxy->upstream_fd, &wfds );
				break;
			case CONNECT_TO_UPSTREAM:
				proxy_disconnect_from_upstream( proxy );
				/* Changes proxy->upstream_fd */
				proxy_start_connect_to_upstream( proxy );

				if ( proxy->upstream_fd == -1 ) {
					warn( SHOW_ERRNO( "Error acquiring socket to upstream" ) );
					continue;
				}

				/* non-blocking connect() */
				select_timeout.tv_sec = 15;
				FD_SET( proxy->upstream_fd, &wfds );
				break;
			case READ_INIT_FROM_UPSTREAM:
			case READ_FROM_UPSTREAM:
				select_timeout.tv_sec = 15;
				FD_SET( proxy->upstream_fd, &rfds );
				break;
		};

		if ( select_timeout.tv_sec > 0 ) {
			select_timeout_ptr = &select_timeout;
		}

		result = sock_try_select( FD_SETSIZE, &rfds, &wfds, NULL, select_timeout_ptr );

		if ( result == -1 ) {
			warn( SHOW_ERRNO( "select() failed: " ) );
			break;
		}

		switch( state ) {
			case READ_FROM_DOWNSTREAM:
				if ( FD_ISSET( proxy->downstream_fd, &rfds ) ) {
					state = proxy_read_from_downstream( proxy, state );
#ifdef PREFETCH
					/* Check if we can fulfil the request from prefetch, or
					 * rewrite the request to fill the prefetch buffer if needed
					 */
					if ( state == WRITE_TO_UPSTREAM ) {
						state = proxy_prefetch_for_request( proxy, state );
					}
#endif
				}
				break;
			case CONNECT_TO_UPSTREAM:
				if ( FD_ISSET( proxy->upstream_fd, &wfds ) ) {
					state = proxy_continue_connecting_to_upstream( proxy, state );
				}
				/* Leaving state untouched will retry connecting to upstream */
				break;
			case READ_INIT_FROM_UPSTREAM:
				state = proxy_read_init_from_upstream( proxy, state );
			case WRITE_TO_UPSTREAM:
				if ( FD_ISSET( proxy->upstream_fd, &wfds ) ) {
					state = proxy_write_to_upstream( proxy, state );
				}
				break;
			case READ_FROM_UPSTREAM:
				if ( FD_ISSET( proxy->upstream_fd, &rfds ) ) {
					state = proxy_read_from_upstream( proxy, state );
				}
# ifdef PREFETCH
				/* Fill the prefetch buffer and rewrite the reply, if needed */
				if ( state == WRITE_TO_DOWNSTREAM ) {
					state = proxy_prefetch_for_reply( proxy, state );
				}
#endif
				break;
			case WRITE_TO_DOWNSTREAM:
				if ( FD_ISSET( proxy->downstream_fd, &wfds ) ) {
					state = proxy_write_to_downstream( proxy, state );
				}
				break;
		}

	}

	info(
		"Finished proxy session on fd %i after %"PRIu64" successful request(s)",
		proxy->downstream_fd, proxy->req_count
	);

	/* Reset these two for the next session */
	proxy->req_count = 0;
	proxy->hello_sent = 0;

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
		sock_set_nonblock( client_fd, 1 );
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

		if ( AF_UNIX == proxy->listen_on.family ) {
			if ( -1 == unlink( proxy->listen_on.un.sun_path ) ) {
				warn( SHOW_ERRNO( "Failed to unlink %s", proxy->listen_on.un.sun_path ) );
			}
		}

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

