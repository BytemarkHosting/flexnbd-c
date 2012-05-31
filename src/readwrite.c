#include "nbdtypes.h"
#include "ioutil.h"
#include "util.h"
#include "params.h" 

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

int socket_connect(struct sockaddr* to)
{
	int fd = socket(to->sa_family == AF_INET ? PF_INET : PF_INET6, SOCK_STREAM, 0);
	SERVER_ERROR_ON_FAILURE(fd, "Couldn't create client socket");
	SERVER_ERROR_ON_FAILURE(connect(fd, to, sizeof(struct sockaddr_in6)),
	  "connect failed");
	return fd;
}

off64_t socket_nbd_read_hello(int fd)
{
	struct nbd_init init;
	SERVER_ERROR_ON_FAILURE(readloop(fd, &init, sizeof(init)),
	  "Couldn't read init");
	if (strncmp(init.passwd, INIT_PASSWD, 8) != 0)
		SERVER_ERROR("wrong passwd");
	if (be64toh(init.magic) != INIT_MAGIC)
		SERVER_ERROR("wrong magic (%x)", be64toh(init.magic));
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
	SERVER_ERROR_ON_FAILURE(readloop(fd, reply, sizeof(*reply)),
	  "Couldn't read reply");
	if (be32toh(reply->magic) != REPLY_MAGIC)
		SERVER_ERROR("Reply magic incorrect (%p)", be32toh(reply->magic));
	if (be32toh(reply->error) != 0)
		SERVER_ERROR("Server replied with error %d", be32toh(reply->error));
	if (strncmp(request->handle, reply->handle, 8) != 0)
		SERVER_ERROR("Did not reply with correct handle");
}

void socket_nbd_read(int fd, off64_t from, int len, int out_fd, void* out_buf)
{
	struct nbd_request request;
	struct nbd_reply   reply;
	
	fill_request(&request, REQUEST_READ, from, len);
	SERVER_ERROR_ON_FAILURE(writeloop(fd, &request, sizeof(request)),
	  "Couldn't write request");
	read_reply(fd, &request, &reply);
	
	if (out_buf) {
		SERVER_ERROR_ON_FAILURE(readloop(fd, out_buf, len), 
		  "Read failed");
	}
	else {
		SERVER_ERROR_ON_FAILURE(
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
	SERVER_ERROR_ON_FAILURE(writeloop(fd, &request, sizeof(request)),
	  "Couldn't write request");
	
	if (in_buf) {
		SERVER_ERROR_ON_FAILURE(writeloop(fd, in_buf, len), 
		  "Write failed");
	}
	else {
		SERVER_ERROR_ON_FAILURE(
			splice_via_pipe_loop(in_fd, fd, len),
			"Splice failed"
		);
	}
	
	read_reply(fd, &request, &reply);
}

#define CHECK_RANGE(error_type) { \
	off64_t size = socket_nbd_read_hello(params->client); \
	if (params->from < 0 || (params->from + params->len) > size) \
		SERVER_ERROR(error_type \
		  " request %d+%d is out of range given size %d", \
		  params->from, params->len, size\
		); \
}
  
void do_read(struct mode_readwrite_params* params)
{
	params->client = socket_connect(&params->connect_to.generic);
	CHECK_RANGE("read");
	socket_nbd_read(params->client, params->from, params->len, 
	  params->data_fd, NULL);
	close(params->client);
}

void do_write(struct mode_readwrite_params* params)
{
	params->client = socket_connect(&params->connect_to.generic);
	CHECK_RANGE("write");
	socket_nbd_write(params->client, params->from, params->len, 
	  params->data_fd, NULL);
	close(params->client);
}

