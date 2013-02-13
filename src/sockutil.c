#include "sockutil.h"
#include "util.h"

size_t sockaddr_size(const struct sockaddr* sa)
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

const char* sockaddr_address_string(const struct sockaddr* sa, char* dest, size_t len)
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

int sock_set_reuseaddr(int fd, int optval)
{
	return setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval) );
}

/* Set the tcp_nodelay option */
int sock_set_tcp_nodelay(int fd, int optval)
{
	return setsockopt( fd, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval) );
}

int sock_try_bind(int fd, const struct sockaddr* sa)
{
	int bind_result;
	char s_address[256];

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
					break;
				default:
					warn( "giving up" );
			}
		}
	} while ( 1 );

	return bind_result;
}

