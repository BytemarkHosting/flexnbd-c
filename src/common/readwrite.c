#include "nbdtypes.h"
#include "ioutil.h"
#include "sockutil.h"
#include "util.h"
#include "serve.h"

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

int socket_connect(struct sockaddr* to, struct sockaddr* from)
{
	int fd = socket(to->sa_family == AF_INET ? PF_INET : PF_INET6, SOCK_STREAM, 0);
	if( fd < 0 ){
		warn( "Couldn't create client socket");
		return -1;
	}

	if (NULL != from) {
		if ( 0 > bind( fd, from, sizeof(struct sockaddr_in6 ) ) ){
			warn( SHOW_ERRNO( "bind() to source address failed" ) );
			if ( 0 > close( fd ) ) {  /* Non-fatal leak */
				warn( SHOW_ERRNO( "Failed to close fd %i", fd ) );
			}
			return -1;
		}
	}

	if ( 0 > sock_try_connect( fd, to, sizeof( struct sockaddr_in6 ), 15 ) ) {
		warn( SHOW_ERRNO( "connect failed" ) );
		if ( 0 > close( fd ) ) { /* Non-fatal leak */
			warn( SHOW_ERRNO( "Failed to close fd %i", fd ) );
		}
		return -1;
	}

	if ( sock_set_tcp_nodelay( fd, 1 ) == -1 ) {
		warn( SHOW_ERRNO( "Failed to set TCP_NODELAY" ) );
	}

	return fd;
}

int nbd_check_hello( struct nbd_init_raw* init_raw, uint64_t* out_size, uint32_t* out_flags )
{
	if ( strncmp( init_raw->passwd, INIT_PASSWD, 8 ) != 0 ) {
		warn( "wrong passwd" );
		goto fail;
	}
	if ( be64toh( init_raw->magic ) != INIT_MAGIC ) {
		warn( "wrong magic (%x)", be64toh( init_raw->magic ) );
		goto fail;
	}

	if ( NULL != out_size ) {
		*out_size = be64toh( init_raw->size );
	}
	
	if ( NULL != out_flags ) {
		*out_flags = be32toh( init_raw->flags );
	}

	return 1;
fail:
	return 0;

}

int socket_nbd_read_hello( int fd, uint64_t* out_size, uint32_t* out_flags )
{
	struct nbd_init_raw init_raw;


	if ( 0 > readloop( fd, &init_raw, sizeof(init_raw) ) ) {
		warn( "Couldn't read init" );
		return 0;
	}

	return nbd_check_hello( &init_raw, out_size, out_flags );
}

void nbd_hello_to_buf( struct nbd_init_raw *buf, off64_t out_size, uint32_t out_flags )
{
	struct nbd_init init;

	memcpy( &init.passwd, INIT_PASSWD, 8 );
	init.magic  = INIT_MAGIC;
	init.size   = out_size;
	init.flags  = out_flags;

	memset( buf, 0, sizeof( struct nbd_init_raw ) ); // ensure reserved is 0s
	nbd_h2r_init( &init, buf );

	return;
}

int socket_nbd_write_hello( int fd, off64_t out_size, uint32_t out_flags )
{
	struct nbd_init_raw init_raw;
	nbd_hello_to_buf( &init_raw, out_size, out_flags );

	if ( 0 > writeloop( fd, &init_raw, sizeof( init_raw ) ) ) {
		warn( SHOW_ERRNO( "failed to write hello to socket" ) );
		return 0;
	}
	return 1;
}

void fill_request(struct nbd_request *request, uint16_t type, uint16_t flags, uint64_t from, uint32_t len)
{
	request->magic  = htobe32(REQUEST_MAGIC);
	request->type   = htobe16(type);
	request->flags  = htobe16(flags);
	request->handle.w = (((uint64_t)rand()) << 32) | ((uint64_t)rand());
	request->from   = htobe64(from);
	request->len    = htobe32(len);
}

void read_reply(int fd, struct nbd_request *request, struct nbd_reply *reply)
{
	struct nbd_reply_raw reply_raw;

	ERROR_IF_NEGATIVE(readloop(fd, &reply_raw, sizeof(struct nbd_reply_raw)),
	  "Couldn't read reply");

	nbd_r2h_reply( &reply_raw, reply );

	if (reply->magic != REPLY_MAGIC) {
		error("Reply magic incorrect (%x)", reply->magic);
	}
	if (reply->error != 0) {
		error("Server replied with error %d", reply->error);
	}
	if (request->handle.w != reply->handle.w) {
		error("Did not reply with correct handle");
	}
}

void wait_for_data( int fd, int timeout_secs )
{
	fd_set fds;
	struct timeval tv = { timeout_secs, 0 };
	int selected;

	FD_ZERO( &fds );
	FD_SET( fd, &fds );

	selected = sock_try_select(
		FD_SETSIZE, &fds, NULL, NULL, timeout_secs >=0 ? &tv : NULL
	);

	FATAL_IF( -1 == selected, "Select failed" );
	ERROR_IF(  0 == selected, "Timed out waiting for reply" );
}


void socket_nbd_read(int fd, uint64_t from, uint32_t len, int out_fd, void* out_buf, int timeout_secs)
{
	struct nbd_request request;
	struct nbd_reply   reply;

	fill_request(&request, REQUEST_READ, 0, from, len);
	FATAL_IF_NEGATIVE(writeloop(fd, &request, sizeof(request)),
	  "Couldn't write request");

	wait_for_data( fd, timeout_secs );
	read_reply(fd, &request, &reply);

	if (out_buf) {
		FATAL_IF_NEGATIVE(readloop(fd, out_buf, len),
		  "Read failed");
	}
	else {
		FATAL_IF_NEGATIVE(
			splice_via_pipe_loop(fd, out_fd, len),
			"Splice failed"
		);
	}
}

void socket_nbd_write(int fd, uint64_t from, uint32_t len, int in_fd, void* in_buf, int timeout_secs)
{
	struct nbd_request request;
	struct nbd_reply   reply;

	fill_request(&request, REQUEST_WRITE, 0, from, len);
	ERROR_IF_NEGATIVE(writeloop(fd, &request, sizeof(request)),
	  "Couldn't write request");

	if (in_buf) {
		ERROR_IF_NEGATIVE(writeloop(fd, in_buf, len),
		  "Write failed");
	}
	else {
		ERROR_IF_NEGATIVE(
			splice_via_pipe_loop(in_fd, fd, len),
			"Splice failed"
		);
	}

	wait_for_data( fd, timeout_secs );
	read_reply(fd, &request, &reply);
}


int socket_nbd_disconnect( int fd )
{
	int success = 1;
	struct nbd_request request;

	fill_request( &request, REQUEST_DISCONNECT, 0, 0, 0 );
	/* FIXME: This shouldn't be a FATAL error.  We should just drop
	 * the mirror without affecting the main server.
	 */
	FATAL_IF_NEGATIVE( writeloop( fd, &request, sizeof( request ) ),
			"Failed to write the disconnect request." );
	return success;
}

#define CHECK_RANGE(error_type) { \
	uint64_t size;\
	uint32_t flags;\
	int success = socket_nbd_read_hello(params->client, &size, &flags); \
	if ( success ) {\
		uint64_t endpoint = params->from + params->len; \
		if (endpoint > size || \
				endpoint < params->from ) { /* this happens on overflow */ \
			fatal(error_type \
			  " request %d+%d is out of range given size %d", \
			  params->from, params->len, size\
			);\
		}\
	}\
	else {\
		fatal( error_type " connection failed." );\
	}\
}

void do_read(struct mode_readwrite_params* params)
{
	params->client = socket_connect(&params->connect_to.generic, &params->connect_from.generic);
	FATAL_IF_NEGATIVE( params->client, "Couldn't connect." );
	CHECK_RANGE("read");
	socket_nbd_read(params->client, params->from, params->len,
	  params->data_fd, NULL, 10);
	close(params->client);
}

void do_write(struct mode_readwrite_params* params)
{
	params->client = socket_connect(&params->connect_to.generic, &params->connect_from.generic);
	FATAL_IF_NEGATIVE( params->client, "Couldn't connect." );
	CHECK_RANGE("write");
	socket_nbd_write(params->client, params->from, params->len,
	  params->data_fd, NULL, 10);
	close(params->client);
}

