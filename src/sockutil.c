#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#include "sockutil.h"
#include "util.h"

size_t sockaddr_size( const struct sockaddr* sa )
{
	size_t ret = 0;

	switch( sa->sa_family ) {
		case AF_INET:
			ret = sizeof( struct sockaddr_in );
			break;
		case AF_INET6:
			ret = sizeof( struct sockaddr_in6 );
			break;
	}

	return ret;
}

const char* sockaddr_address_string( const struct sockaddr* sa, char* dest, size_t len )
{
	NULLCHECK( sa );
	NULLCHECK( dest );

	struct sockaddr_in*  in  = ( struct sockaddr_in*  ) sa;
	struct sockaddr_in6* in6 = ( struct sockaddr_in6* ) sa;

	unsigned short real_port = ntohs( in->sin_port ); // common to in and in6
	size_t size;
	const char* ret = NULL;

	memset( dest, 0, len );

	if ( sa->sa_family == AF_INET ) {
		ret = inet_ntop( AF_INET, &in->sin_addr, dest, len );
	} else if ( sa->sa_family == AF_INET6 ) {
		ret = inet_ntop( AF_INET6, &in6->sin6_addr, dest, len );
	} else {
		strncpy( dest, "???", len );
	}

	if ( NULL != ret && real_port > 0 ) {
		size = strlen( dest );
		snprintf( dest + size, len - size, " port %d", real_port );
	}

	return ret;
}

int sock_set_reuseaddr( int fd, int optval )
{
	return setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval) );
}

/* Set the tcp_nodelay option */
int sock_set_tcp_nodelay( int fd, int optval )
{
	return setsockopt( fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval) );
}

int sock_set_nonblock( int fd, int optval )
{
	int flags = fcntl( fd, F_GETFL );

	if ( flags == -1 ) {
		return -1;
	}

	if ( optval ) {
		flags = flags | O_NONBLOCK;
	} else {
		flags = flags & (~O_NONBLOCK);
	}

	return fcntl( fd, F_SETFL, flags );
}

int sock_try_bind( int fd, const struct sockaddr* sa )
{
	int bind_result;
	char s_address[256];
	int retry = 1;

	sockaddr_address_string( sa, &s_address[0], 256 );

	do {
		bind_result = bind( fd, sa, sockaddr_size( sa ) );
		if ( 0 == bind_result ) {
			info( "Bound to %s", s_address );
			break;
		}
		else {
			warn( SHOW_ERRNO( "Couldn't bind to %s", s_address ) );

			switch ( errno ) {
				/* bind() can give us EACCES, EADDRINUSE, EADDRNOTAVAIL, EBADF,
				 * EINVAL, ENOTSOCK, EFAULT, ELOOP, ENAMETOOLONG, ENOENT,
				 * ENOMEM, ENOTDIR, EROFS
				 *
				 * Any of these other than  EADDRINUSE & EADDRNOTAVAIL signify
				 * that there's a logic error somewhere.
				 *
				 * EADDRINUSE is fatal: if there's something already where we
				 * want to be listening, we have no guarantees that any clients
				 * will cope with it.
				 */
				case EADDRNOTAVAIL:
					debug( "retrying" );
					sleep( 1 );
					continue;
				case EADDRINUSE:
					warn( "%s in use, giving up.", s_address );
					retry = 0;
					break;
				default:
					warn( "giving up" );
					retry = 0;
			}
		}
	} while ( retry );

	return bind_result;
}

int sock_try_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
	int result;

	do {
		result = select(nfds, readfds, writefds, exceptfds, timeout);
		if ( errno != EINTR ) {
			break;
		}

	} while ( result == -1 );

	return result;
}

int sock_try_connect( int fd, struct sockaddr* to, socklen_t addrlen, int wait )
{
	fd_set fds;
	struct timeval tv = { wait, 0 };
	int result = 0;

	if ( sock_set_nonblock( fd, 1 ) == -1 ) {
		warn( SHOW_ERRNO( "Failed to set socket non-blocking for connect()" ) );
		return connect( fd, to, addrlen );
	}

	FD_ZERO( &fds );
	FD_SET( fd, &fds );

	do {
		result = connect( fd, to, addrlen );

		if ( result == -1 ) {
			switch( errno ) {
				case EINPROGRESS:
					result = 0;
					break; /* success */
				case EAGAIN:
				case EINTR:
					break; /* Try again */
				default:
					warn( SHOW_ERRNO( "Failed to connect()") );
					goto out;
			}
		}
	} while ( result == -1 );

	if ( -1 == sock_try_select( FD_SETSIZE, NULL, &fds, NULL, &tv) ) {
		warn( SHOW_ERRNO( "failed to select() on non-blocking connect" ) );
		result = -1;
		goto out;
	}

	if ( !FD_ISSET( fd, &fds ) ) {
		result = -1;
		errno = ETIMEDOUT;
		goto out;
	}

	int scratch;
	socklen_t s_size = sizeof( scratch );
	if ( getsockopt( fd, SOL_SOCKET, SO_ERROR, &scratch, &s_size ) == -1 ) {
		result = -1;
		warn( SHOW_ERRNO( "getsockopt() failed" ) );
		goto out;
	}

	if ( scratch == EINPROGRESS ) {
		scratch = ETIMEDOUT;
	}

	result = scratch ? -1 : 0;
	errno = scratch;

out:
	if ( sock_set_nonblock( fd, 0 ) == -1 ) {
		warn( SHOW_ERRNO( "Failed to make socket blocking after connect()" ) );
		return -1;
	}

	debug( "sock_try_connect: %i", result );
	return result;
}

