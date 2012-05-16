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
	"        flexnbd write  <IP address> <port> <offset> <length> < data\n"
	"        flexnbd write  <IP address> <port> <offset> <data file>\n"
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
		exit(1);
	else
		pthread_exit((void*) 1);
}

#ifndef DEBUG
#  define debug(msg, ...)
#else
#  include <sys/times.h>
#  define debug(msg, ...) fprintf(stderr, "%08x %4d: " msg "\n" , \
     (int) pthread_self(), (int) clock(), ##__VA_ARGS__)
#endif

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

union mysockaddr {
	unsigned short      family;
	struct sockaddr     generic;
        struct sockaddr_in  v4;
        struct sockaddr_in6 v6;
};

struct ip_and_mask {
	union mysockaddr ip;
	int              mask;
};

struct mode_serve_params {
	union mysockaddr     bind_to;
	int                  acl_entries;
	struct ip_and_mask** acl;
	char*                filename;
	int                  tcp_backlog;
	
	int     server;
	int     threads;
};

struct mode_readwrite_params {
	union mysockaddr     connect_to;
	off64_t              from;
	off64_t              len;
	int                  data_fd;
	int                  client;
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
	struct mode_readwrite_params readwrite;
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
		if (result == 0 /* EOF */ || result == -1 /* error */)
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

int splice_via_pipe_loop(int fd_in, int fd_out, size_t len)
{
	int pipefd[2];
	size_t spliced=0;
	
	if (pipe(pipefd) == -1)
		return -1;
	
	while (spliced < len) {
		size_t r1,r2;
		r1 = splice(fd_in, NULL, pipefd[1], NULL, len-spliced, 0);
		if (r1 <= 0)
			break;
		r2 = splice(pipefd[0], NULL, fd_out, NULL, r1, 0);
		if (r1 != r2)
			break;
		spliced += r1;
	}
	close(pipefd[0]);
	close(pipefd[1]);
	
	return spliced < len ? -1 : 0;
}

int client_serve_request(struct client_params* client)
{
	off64_t               offset;
	struct nbd_request    request;
	struct nbd_reply      reply;
	
	if (readloop(client->socket, &request, sizeof(request)) == -1) {
		if (errno == 0) {
			debug("EOF reading request");
			return 1; /* neat point to close the socket */
		}
		else {
			CLIENT_ERROR_ON_FAILURE(-1, "Error reading request");
		}
	}
	
	reply.magic = htobe32(REPLY_MAGIC);
	reply.error = htobe32(0);
	memcpy(reply.handle, request.handle, 8);
	
	debug("request type %d", be32toh(request.type));
	
	if (be32toh(request.magic) != REQUEST_MAGIC)
		CLIENT_ERROR("Bad magic %08x", be32toh(request.magic));
		
	switch (be32toh(request.type))
	{
	case REQUEST_READ:
	case REQUEST_WRITE:
		/* check it's not out of range */
		if (be64toh(request.from) < 0 || 
		    be64toh(request.from)+be32toh(request.len) > client->size) {
			debug("request read %ld+%d out of range", 
			  be64toh(request.from), 
			  be32toh(request.len)
			);
			reply.error = htobe32(1);
			write(client->socket, &reply, sizeof(reply));
			return 0;
		}
		break;
		
	case REQUEST_DISCONNECT:
		debug("request disconnect");
		return 1;
		
	default:
		CLIENT_ERROR("Unknown request %08x", be32toh(request.type));
	}
	
	switch (be32toh(request.type))
	{
	case REQUEST_READ:
		debug("request read %ld+%d", be64toh(request.from), be32toh(request.len));
		write(client->socket, &reply, sizeof(reply));
		
		offset = be64toh(request.from);
		CLIENT_ERROR_ON_FAILURE(
			sendfileloop(
				client->socket, 
				client->fileno, 
				&offset, 
				be32toh(request.len)
			),
			"sendfile failed from=%ld, len=%d",
			offset,
			be32toh(request.len)
		);
		break;
		
	case REQUEST_WRITE:
		debug("request write %ld+%d", be64toh(request.from), be32toh(request.len));
		CLIENT_ERROR_ON_FAILURE(
			readloop(
				client->socket,
				client->mapped + be64toh(request.from),
				be32toh(request.len)
			),
			"read failed from=%ld, len=%d",
			be64toh(request.from),
			be32toh(request.len)
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
	debug("opened %s size %ld on fd %d @ %p", client->filename, client->size, client->fileno, client->mapped);
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
		;
		
	CLIENT_ERROR_ON_FAILURE(
		close(client->socket),
		"Couldn't close socket %d", 
		client->socket
	);
	
	free(client);
	return NULL;
}

/* FIXME */
static int testmasks[9] = { 0,128,192,224,240,248,252,254,255 };

int is_included_in_acl(int list_length, struct ip_and_mask** list, struct sockaddr* test)
{
	int i;
	
	for (i=0; i < list_length; i++) {
		struct ip_and_mask *entry = list[i];
		int testbits;
		char *raw_address1, *raw_address2;
		
		debug("checking acl entry %d", i);
		
		if (test->sa_family != entry->ip.family)
			continue;
		
		if (test->sa_family == AF_INET) {
			raw_address1 = (char*) 
			  &((struct sockaddr_in*) test)->sin_addr;
			raw_address2 = (char*) &entry->ip.v4.sin_addr;
		}
		else if (test->sa_family == AF_INET6) {
			raw_address1 = (char*) 
			  &((struct sockaddr_in6*) test)->sin6_addr;
			raw_address2 = (char*) &entry->ip.v6.sin6_addr;
		}
		
		for (testbits = entry->mask; testbits > 0; testbits -= 8) {
			debug("testbits=%d, c1=%d, c2=%d", testbits, raw_address1[0], raw_address2[0]);
			if (testbits >= 8) {
				if (raw_address1[0] != raw_address2[0])
					goto no_match;
			}
			else {
				if ((raw_address1[0] & testmasks[testbits%8]) !=
				    (raw_address2[0] & testmasks[testbits%8]) )
				    	goto no_match;
			}
			
			raw_address1++;
			raw_address2++;
		}
		
		return 1;
		
		no_match: ;
		debug("no match");
	}
	
	return 0;
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
		    !is_included_in_acl(params->acl_entries, params->acl, &client_address)) {
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

void do_serve(struct mode_serve_params* params)
{
	serve_open_socket(params);
	serve_accept_loop(params);
}

int socket_connect(struct sockaddr* to)
{
	int fd = socket(PF_INET, SOCK_STREAM, 0);
	SERVER_ERROR_ON_FAILURE(fd, "Couldn't create client socket");
	SERVER_ERROR_ON_FAILURE(connect(fd, to, sizeof(*to)),
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
		SERVER_ERROR("Reply magic incorrect (%p)", reply->magic);
	if (be32toh(reply->error) != 0)
		SERVER_ERROR("Server replied with error %d", reply->error);
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
	if (params->from < 0 || (params->from + params->len) >= size) \
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

#define IS_IP_VALID_CHAR(x) ( ((x) >= '0' && (x) <= '9' ) || \
                              ((x) >= 'a' && (x) <= 'f')  || \
                              ((x) >= 'A' && (x) <= 'F' ) || \
                               (x) == ':' || (x) == '.'      \
                            )
int parse_ip_to_sockaddr(struct sockaddr* out, char* src)
{
	char temp[64];
	struct sockaddr_in *v4  = (struct sockaddr_in *) out;	
	struct sockaddr_in6 *v6 = (struct sockaddr_in6 *) out;
	
	/* allow user to start with [ and end with any other invalid char */
	{
		int i=0, j=0;
		if (src[i] == '[')
			i++;
		for (; i<64 && IS_IP_VALID_CHAR(src[i]); i++)
			temp[j++] = src[i];
		temp[j] = 0;
	}
	
	if (temp[0] == '0' && temp[1] == '\0') {
		v4->sin_family = AF_INET;
		v4->sin_addr.s_addr = INADDR_ANY;
		return 1;
	}

	if (inet_pton(AF_INET, temp, &v4->sin_addr) == 1) {
		out->sa_family = AF_INET;
		return 1;
	}
	
	if (inet_pton(AF_INET6, temp, &v6->sin6_addr) == 1) {
		out->sa_family = AF_INET6;
		return 1;
	}
	
	return 0;
}

int parse_acl(struct ip_and_mask*** out, int max, char **entries)
{
	int i;
	
	if (max == 0) {
		*out = NULL;
		return 0;
	}
	
	for (i = 0; i < max; i++) {
#               define MAX_MASK_BITS (outentry->ip.family == AF_INET ? 32 : 128)
		int j;
		struct ip_and_mask* outentry = (*out)[i];
		
		if (parse_ip_to_sockaddr(&outentry->ip.generic, entries[i]) == 0)
			return i;
			
		for (j=0; entries[i][j] && entries[i][j] != '/'; j++)
			;
		if (entries[i][j] == '/') {
			outentry->mask = atoi(entries[i]+j+1);
			if (outentry->mask < 1 || outentry->mask > MAX_MASK_BITS)
				return i;
		}
		else
			outentry->mask = MAX_MASK_BITS;
#		undef MAX_MASK_BITS

		debug("acl entry %d has mask %d", i, outentry->mask);
	}
	
	return max;
}

void params_serve(
	struct mode_serve_params* out, 
	char* s_ip_address, 
	char* s_port, 
	char* s_file,
	int acl_entries,
	char** s_acl_entries
)
{
	int parsed;
	
	out->tcp_backlog = 10; /* does this need to be settable? */
	
	if (s_ip_address == NULL)
		SERVER_ERROR("No IP address supplied");
	if (s_port == NULL)
		SERVER_ERROR("No port number supplied");
	if (s_file == NULL)
		SERVER_ERROR("No filename supplied");
	
	if (parse_ip_to_sockaddr(&out->bind_to.generic, s_ip_address) == 0)
		SERVER_ERROR("Couldn't parse server address '%s' (use 0 if "
		  "you want to bind to all IPs)", s_ip_address);
	
	out->acl_entries = acl_entries;
	parsed = parse_acl(&out->acl, acl_entries, s_acl_entries);
	if (parsed != acl_entries)
		SERVER_ERROR("Bad ACL entry '%s'", s_acl_entries[parsed]);
	
	debug("%p %d", out->acl, out->acl_entries);
	
	out->bind_to.v4.sin_port = atoi(s_port);
	if (out->bind_to.v4.sin_port < 0 || out->bind_to.v4.sin_port > 65535)
		SERVER_ERROR("Port number must be >= 0 and <= 65535");
	out->bind_to.v4.sin_port = htobe16(out->bind_to.v4.sin_port);
	
	out->filename = s_file;
}

void params_readwrite(
	int write_not_read,
	struct mode_readwrite_params* out,
	char* s_ip_address, 
	char* s_port,
	char* s_from,
	char* s_length_or_filename
)
{
	if (s_ip_address == NULL)
		SERVER_ERROR("No IP address supplied");
	if (s_port == NULL)
		SERVER_ERROR("No port number supplied");
	if (s_from == NULL)
		SERVER_ERROR("No from supplied");
	if (s_length_or_filename == NULL)
		SERVER_ERROR("No length supplied");
	
	if (parse_ip_to_sockaddr(&out->connect_to.generic, s_ip_address) == 0)
		SERVER_ERROR("Couldn't parse connection address '%s'", 
		s_ip_address);
	
	out->connect_to.v4.sin_port = atoi(s_port);
	if (out->connect_to.v4.sin_port < 0 || out->connect_to.v4.sin_port > 65535)
		SERVER_ERROR("Port number must be >= 0 and <= 65535");
	out->connect_to.v4.sin_port = htobe16(out->connect_to.v4.sin_port);

	out->from  = atol(s_from);
	
	if (write_not_read) {
		if (s_length_or_filename[0]-48 < 10) {
			out->len   = atol(s_length_or_filename);
			out->data_fd = 0;
		}
		else {
			out->data_fd = open(
			  s_length_or_filename, O_RDONLY);
			SERVER_ERROR_ON_FAILURE(out->data_fd,
			  "Couldn't open %s", s_length_or_filename);
			out->len = lseek64(out->data_fd, 0, SEEK_END);
			SERVER_ERROR_ON_FAILURE(out->len,
			  "Couldn't find length of %s", s_length_or_filename);
			SERVER_ERROR_ON_FAILURE(
				lseek64(out->data_fd, 0, SEEK_SET),
				"Couldn't rewind %s", s_length_or_filename
			);
		}
	}
	else {
		out->len = atol(s_length_or_filename);
		out->data_fd = 1;
	}
}

void mode(char* mode, int argc, char **argv)
{
	union mode_params params;
	
	if (strcmp(mode, "serve") == 0) {
		if (argc >= 3) {
			params_serve(&params.serve, argv[0], argv[1], argv[2], argc-3, argv+3);
			do_serve(&params.serve);
		}
		else {
			syntax();
		}
	}
	else if (strcmp(mode, "read") == 0 ) {
		if (argc == 4) {
			params_readwrite(0, &params.readwrite, argv[0], argv[1], argv[2], argv[3]);
			do_read(&params.readwrite);
		}
		else {
			syntax();
		}
	}
	else if (strcmp(mode, "write") == 0 ) {
		if (argc == 4) {
			params_readwrite(1, &params.readwrite, argv[0], argv[1], argv[2], argv[3]);
			do_write(&params.readwrite);
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

