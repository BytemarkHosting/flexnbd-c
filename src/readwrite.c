#include "nbdtypes.h"
#include "ioutil.h"
#include "util.h"
#include "serve.h" 

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

int socket_connect(struct sockaddr* to, struct sockaddr* from)
{
	int fd = socket(to->sa_family == AF_INET ? PF_INET : PF_INET6, SOCK_STREAM, 0);
	FATAL_IF_NEGATIVE(fd, "Couldn't create client socket");

	if (NULL != from) {
		FATAL_IF_NEGATIVE(
			bind(fd, from, sizeof(struct sockaddr_in6)),
			"bind() failed"
		);
	}

	FATAL_IF_NEGATIVE(
		connect(fd, to, sizeof(struct sockaddr_in6)),"connect failed"
	);
	return fd;
}

off64_t socket_nbd_read_hello(int fd)
{
	struct nbd_init init;
	FATAL_IF_NEGATIVE(readloop(fd, &init, sizeof(init)),
	  "Couldn't read init");
	if (strncmp(init.passwd, INIT_PASSWD, 8) != 0) {
		fatal("wrong passwd");
	}
	if (be64toh(init.magic) != INIT_MAGIC) {
		fatal("wrong magic (%x)", be64toh(init.magic));
	}
	return be64toh(init.size);
}

void fill_request(struct nbd_request *request, int type, int from, int len)
{
	request->magic  = htobe32(REQUEST_MAGIC);
	request->type   = htobe32(type);
	((int*) request->handle)[0] = rand();
	((int*) request->handle)[1] = rand();
	request->from   = htobe64(from);
	request->len    = htobe32(len);
}

void read_reply(int fd, struct nbd_request *request, struct nbd_reply *reply)
{
	FATAL_IF_NEGATIVE(readloop(fd, reply, sizeof(*reply)),
	  "Couldn't read reply");
	if (be32toh(reply->magic) != REPLY_MAGIC) {
		fatal("Reply magic incorrect (%p)", be32toh(reply->magic));
	}
	if (be32toh(reply->error) != 0) {
		fatal("Server replied with error %d", be32toh(reply->error));
	}
	if (strncmp(request->handle, reply->handle, 8) != 0) {
		fatal("Did not reply with correct handle");
	}
}

void socket_nbd_read(int fd, off64_t from, int len, int out_fd, void* out_buf)
{
	struct nbd_request request;
	struct nbd_reply   reply;
	
	fill_request(&request, REQUEST_READ, from, len);
	FATAL_IF_NEGATIVE(writeloop(fd, &request, sizeof(request)),
	  "Couldn't write request");
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

void socket_nbd_write(int fd, off64_t from, int len, int in_fd, void* in_buf)
{
	struct nbd_request request;
	struct nbd_reply   reply;
	
	fill_request(&request, REQUEST_WRITE, from, len);
	FATAL_IF_NEGATIVE(writeloop(fd, &request, sizeof(request)),
	  "Couldn't write request");
	
	if (in_buf) {
		FATAL_IF_NEGATIVE(writeloop(fd, in_buf, len), 
		  "Write failed");
	}
	else {
		FATAL_IF_NEGATIVE(
			splice_via_pipe_loop(in_fd, fd, len),
			"Splice failed"
		);
	}
	
	read_reply(fd, &request, &reply);
}


void socket_nbd_entrust( int fd )
{
	struct nbd_request request;
	struct nbd_reply   reply;

	fill_request( &request, REQUEST_ENTRUST, 0, 0 );
	FATAL_IF_NEGATIVE( writeloop( fd, &request, sizeof( request ) ),
			"Couldn't write request");
	read_reply( fd, &request, &reply );
}


int socket_nbd_disconnect( int fd )
{
	int success = 1;
	struct nbd_request request;

	fill_request( &request, REQUEST_DISCONNECT, 0, 0 );
	/* FIXME: This shouldn't be a FATAL error.  We should just drop
	 * the mirror without affecting the main server.
	 */
	FATAL_IF_NEGATIVE( writeloop( fd, &request, sizeof( request ) ),
			"Failed to write the disconnect request." );
	return success;
}

#define CHECK_RANGE(error_type) { \
	off64_t size = socket_nbd_read_hello(params->client); \
	if (params->from < 0 || (params->from + params->len) > size) {\
		fatal(error_type \
		  " request %d+%d is out of range given size %d", \
		  params->from, params->len, size\
		); }\
}
  
void do_read(struct mode_readwrite_params* params)
{
	params->client = socket_connect(&params->connect_to.generic, &params->connect_from.generic);
	CHECK_RANGE("read");
	socket_nbd_read(params->client, params->from, params->len, 
	  params->data_fd, NULL);
	close(params->client);
}

void do_write(struct mode_readwrite_params* params)
{
	params->client = socket_connect(&params->connect_to.generic, &params->connect_from.generic);
	CHECK_RANGE("write");
	socket_nbd_write(params->client, params->from, params->len, 
	  params->data_fd, NULL);
	close(params->client);
}

