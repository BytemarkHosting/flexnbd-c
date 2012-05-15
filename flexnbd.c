#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <malloc.h>
#include <errno.h>
#include <endian.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/mman.h>
#include <sys/sendfile.h>

/* http://linux.derkeiler.com/Mailing-Lists/Kernel/2003-09/2332.html */
#define INIT_PASSWD "NBDMAGIC" 
#define INIT_MAGIC 0x0000420281861253 
#define REQUEST_MAGIC 0x25609513 
#define REPLY_MAGIC 0x67446698 
#define REQUEST_READ 0 
#define REQUEST_WRITE 1 
#define REQUEST_DISCONNECT 2 

#include <linux/types.h>
struct nbd_init {
	char passwd[8];
	__be64 magic;
	__be64 size;
	char reserved[128];
};

struct nbd_request {
        __be32 magic;
        __be32 type;    /* == READ || == WRITE  */
        char handle[8];
        __be64 from;
        __be32 len;
} __attribute__((packed));

struct nbd_reply {
        __be32 magic;
        __be32 error;           /* 0 = ok, else error   */
        char handle[8];         /* handle you got from request  */
};

void syntax()
{
	fprintf(stderr, 
	"Syntax: flexnbd serve  <IP address> <port> <file> [ip addresses ...]\n"
	"        flexnbd read   <IP address> <port> <offset> <length> > data\n"
	"        flexnbd write  <IP address> <port> <offset> [length] < data\n"
	"        flexnbd mirror <IP address> <port> <target IP> <target port>\n"
	);
	exit(1);
}

static pthread_t server_thread_id;

void error(int consult_errno, int close_socket, const char* format, ...)
{
	va_list argptr;
	
	fprintf(stderr, "*** ");
	
	va_start(argptr, format);
	vfprintf(stderr, format, argptr);
	va_end(argptr);
	
	if (consult_errno) {
		fprintf(stderr, " (errno=%d, %s)", errno, strerror(errno));
	}
	
	if (close_socket)
		close(close_socket);
	
	fprintf(stderr, "\n");
	
	if (pthread_equal(pthread_self(), server_thread_id))
		pthread_exit((void*) 1);
	else
		exit(1);
}

#define CLIENT_ERROR(msg, ...) \
  error(0, client->socket, msg, ##__VA_ARGS__)
#define CLIENT_ERROR_ON_FAILURE(test, msg, ...) \
  if (test < 0) { error(1, client->socket, msg, ##__VA_ARGS__); }
#define SERVER_ERROR(msg, ...) \
  error(0, 0, msg, ##__VA_ARGS__)
#define SERVER_ERROR_ON_FAILURE(test, msg, ...) \
  if (test < 0) { error(1, 0, msg, ##__VA_ARGS__); }

void* xmalloc(size_t size)
{
	void* p = malloc(size);
	if (p == NULL)
		SERVER_ERROR("couldn't malloc %d bytes", size);
	return p;
}

struct ip_and_mask {
	/* FIXME */
};

struct mode_serve_params {
	union { struct sockaddr     generic;
	        struct sockaddr_in  v4;
	        struct sockaddr_in6 v6; } bind_to;
	struct ip_and_mask**             acl;
	char*                            filename;
	int                              tcp_backlog;
	
	int     server;
	int     threads;
};

struct client_params {
	int     socket;
	char*   filename;
	
	int     fileno;
	off64_t size;
	char*   mapped;
};

union mode_params {
	struct mode_serve_params serve;
};

int writeloop(int filedes, const void *buffer, size_t size)
{
	size_t written=0;
	while (written < size) {
		size_t result = write(filedes, buffer+written, size-written);
		if (result == -1)
			return -1;
		written += result;
	}
	return 0;
}

int readloop(int filedes, void *buffer, size_t size)
{
	size_t readden=0;
	while (readden < size) {
		size_t result = read(filedes, buffer+readden, size-readden);
		printf("read size=%d readden=%d result=%d\n", size, readden, result);
		if (result == -1)
			return -1;
		readden += result;
	}
	return 0;
}

int sendfileloop(int out_fd, int in_fd, off64_t *offset, size_t count)
{
	size_t sent=0;
	while (sent < count) {
		size_t result = sendfile64(out_fd, in_fd, offset+sent, count-sent);
		if (result == -1)
			return -1;
		sent += result;
	}
	return 0;
}

int client_serve_request(struct client_params* client)
{
	off64_t               offset;
	struct nbd_request    request;
	struct nbd_reply      reply;
	
	CLIENT_ERROR_ON_FAILURE(
		readloop(client->socket, &request, sizeof(request)),
		"Failed to read request"
	);
	
	reply.magic = htobe32(REPLY_MAGIC);
	reply.error = htobe32(0);
	memcpy(reply.handle, request.handle, 8);
	
	if (be32toh(request.magic) != REQUEST_MAGIC)
		CLIENT_ERROR("Bad magic %08x", be32toh(request.magic));
		
	switch (be32toh(request.type))
	{
	case REQUEST_READ:
	case REQUEST_WRITE:
		/* check it's not out of range */
		if (be64toh(request.from) < 0 || 
		    be64toh(request.from)+be64toh(request.len) > client->size) {
			reply.error = htobe32(1);
			write(client->socket, &reply, sizeof(reply));
			return 0;
		}
	case REQUEST_DISCONNECT:
		return 1;
	default:
		CLIENT_ERROR("Unknown request %08x", be32toh(request.type));
	}
	
	switch (be32toh(request.type))
	{
	case REQUEST_READ:
		write(client->socket, &reply, sizeof(reply));
		
		offset = be64toh(request.from);
		CLIENT_ERROR_ON_FAILURE(
			sendfileloop(
				client->socket, 
				client->fileno, 
				&offset, 
				be64toh(request.len)
			),
			"sendfile failed from=%ld, len=%ld",
			offset,
			be64toh(request.len)
		);
		break;
		
	case REQUEST_WRITE:
		CLIENT_ERROR_ON_FAILURE(
			readloop(
				client->socket,
				client->mapped + be64toh(request.from),
				be64toh(request.len)
			),
			"read failed from=%ld, len=%d",
			be64toh(request.from),
			be64toh(request.len)
		);
		write(client->socket, &reply, sizeof(reply));
		
		break;
	}
	return 0;
}

void client_open_file(struct client_params* client)
{
	client->fileno = open(client->filename, O_RDWR|O_DIRECT|O_SYNC);
	CLIENT_ERROR_ON_FAILURE(client->fileno, "Couldn't open %s", 
	  client->filename);
	client->size = lseek64(client->fileno, 0, SEEK_END);
	CLIENT_ERROR_ON_FAILURE(client->fileno, "Couldn't seek to end of %s",
	  client->filename);
	client->mapped = mmap64(NULL, client->size, PROT_READ|PROT_WRITE, 
	  MAP_SHARED, client->fileno, 0);
	CLIENT_ERROR_ON_FAILURE((long) client->mapped, "Couldn't map file %s",
	  client->filename);
}

void client_send_hello(struct client_params* client)
{
	struct nbd_init init;
	
	memcpy(init.passwd, INIT_PASSWD, sizeof(INIT_PASSWD));
	init.magic = htobe64(INIT_MAGIC);
	init.size = htobe64(client->size);
	memset(init.reserved, 0, 128);
	CLIENT_ERROR_ON_FAILURE(
		writeloop(client->socket, &init, sizeof(init)),
		"Couldn't send hello"
	);
}

void* client_serve(void* client_uncast)
{
	struct client_params* client = (struct client_params*) client_uncast;
	
	client_open_file(client);
	client_send_hello(client);
	
	while (client_serve_request(client) == 0)
	
	CLIENT_ERROR_ON_FAILURE(
		close(client->socket),
		"Couldn't close socket %d", 
		client->socket
	);
	
	free(client);
	return NULL;
}

/* FIXME */
int is_included_in_acl(struct ip_and_mask** list, struct sockaddr* test)
{
	return 1;
}

void serve_open_socket(struct mode_serve_params* params)
{
	params->server = socket(PF_INET, SOCK_STREAM, 0);
	
	SERVER_ERROR_ON_FAILURE(params->server, 
	  "Couldn't create server socket");
	  
	SERVER_ERROR_ON_FAILURE(
		bind(params->server, &params->bind_to.generic, 
		  sizeof(params->bind_to.generic)),
		"Couldn't bind server to IP address"
	);
	
	SERVER_ERROR_ON_FAILURE(
		listen(params->server, params->tcp_backlog),
		"Couldn't listen on server socket"
	);
}

void serve_accept_loop(struct mode_serve_params* params) 
{
	while (1) {
		pthread_t client_thread;
		struct sockaddr client_address;
		struct client_params* client_params;
		socklen_t socket_length;
		
		int client_socket = accept(params->server, &client_address, 
		  &socket_length);
		  
		SERVER_ERROR_ON_FAILURE(client_socket, "accept() failed");
		
		if (params->acl && 
		    !is_included_in_acl(params->acl, &client_address)) {
			write(client_socket, "Access control error", 20);
			close(client_socket);
			continue;
		}
		
		client_params = xmalloc(sizeof(struct client_params));
		client_params->socket = client_socket;
		client_params->filename = params->filename;
		
		client_thread = pthread_create(&client_thread, NULL, 
		  client_serve, client_params);
		SERVER_ERROR_ON_FAILURE(client_thread,
		  "Failed to create client thread");
		/* FIXME: keep track of them? */
		/* FIXME: maybe shouldn't be fatal? */
	}
}

void serve(struct mode_serve_params* params)
{
	serve_open_socket(params);
	serve_accept_loop(params);
}

void params_serve(
	struct mode_serve_params* out, 
	char* s_ip_address, 
	char* s_port, 
	char* s_file
)
{
	out->tcp_backlog = 10; /* does this need to be settable? */
	out->acl = NULL; /* ignore for now */
	
	if (s_ip_address == NULL)
		SERVER_ERROR("No IP address supplied");
	if (s_port == NULL)
		SERVER_ERROR("No port number supplied");
	if (s_file == NULL)
		SERVER_ERROR("No filename supplied");
	
	if (s_ip_address[0] == '0' && s_ip_address[1] == '\0') {
		out->bind_to.v4.sin_family = AF_INET;
		out->bind_to.v4.sin_addr.s_addr = INADDR_ANY;
	}
	else if (inet_pton(AF_INET, s_ip_address, &out->bind_to.v4) == 0) {
	}
	else if (inet_pton(AF_INET6, s_ip_address, &out->bind_to.v6) == 0) {
	}
	else {
		SERVER_ERROR("Couldn't understand address '%%' "
		  "(use 0 if you don't care)", s_ip_address);
	}
	
	out->bind_to.v4.sin_port = atoi(s_port);
	if (out->bind_to.v4.sin_port < 0 || out->bind_to.v4.sin_port > 65535)
		SERVER_ERROR("Port number must be >= 0 and <= 65535");
	out->bind_to.v4.sin_port = htobe16(out->bind_to.v4.sin_port);
	
	out->filename = s_file;
}

void mode(char* mode, int argc, char **argv)
{
	union mode_params params;
	
	if (strcmp(mode, "serve") == 0) {
		if (argc >= 3) {
			params_serve(&params.serve, argv[0], argv[1], argv[2]);
			serve(&params.serve);
		}
		else {
			syntax();
		}
	}
	else {
		syntax();
	}
	exit(0);
}

int main(int argc, char** argv)
{
	server_thread_id = pthread_self();
	
	if (argc < 2)
		syntax();
	mode(argv[1], argc-2, argv+2);
	
	return 0;
}

