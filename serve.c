#include "params.h"
#include "nbdtypes.h"
#include "ioutil.h"
#include "util.h"
#include "bitset.h"
#include "parse.h"
#include "readwrite.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/un.h>
#include <fcntl.h>

#include <string.h>
#include <stdlib.h>
#include <errno.h>

static const int block_allocation_resolution = 4096;//128<<10;

static inline void dirty(struct mode_serve_params *serve, off64_t from, int len)
{
	if (serve->mirror)
		bitset_set_range(serve->mirror->dirty_map, from, len);
}

/**
 * So waiting on client->socket is len bytes of data, and we must write it all
 * to client->mapped.  However while doing do we must consult the bitmap
 * client->block_allocation_map, which is a bitmap where one bit represents
 * block_allocation_resolution bytes.  Where a bit isn't set, there are no 
 * disc blocks allocated for that portion of the file, and we'd like to keep
 * it that way.  
 *
 * If the bitmap shows that every block in our prospective write is already
 * allocated, we can proceed as normal and make one call to writeloop.  
 * 
 */
void write_not_zeroes(struct client_params* client, off64_t from, int len)
{
	char *map = client->block_allocation_map;

	while (len > 0) {
		/* so we have to calculate how much of our input to consider
		 * next based on the bitmap of allocated blocks.  This will be
		 * at a coarser resolution than the actual write, which may
		 * not fall on a block boundary at either end.  So we look up
		 * how many blocks our write covers, then cut off the start
		 * and end to get the exact number of bytes.
		 */
		int first_bit = from/block_allocation_resolution;
		int last_bit  = (from+len+block_allocation_resolution-1) / 
		  block_allocation_resolution;
		int run = bit_run_count(map, first_bit, last_bit-first_bit) *
		  block_allocation_resolution;
		
		if (run > len)
			run = len;
		
		debug("write_not_zeroes: %ld+%d, first_bit=%d, last_bit=%d, run=%d", 
		  from, len, first_bit, last_bit, run);
		
		#define DO_READ(dst, len) CLIENT_ERROR_ON_FAILURE( \
			readloop( \
				client->socket, \
				(dst), \
				(len) \
			), \
			"read failed %ld+%d", from, (len) \
		)
		
		if (bit_is_set(map, from/block_allocation_resolution)) {
			debug("writing the lot");
			/* already allocated, just write it all */
			DO_READ(client->mapped + from, run);
			dirty(client->serve, from, run);
			len  -= run;
			from += run;
		}
		else {
			char zerobuffer[block_allocation_resolution];
			/* not allocated, read in block_allocation_resoution */
			while (run > 0) {
				char *dst = client->mapped+from;
				int bit = from/block_allocation_resolution;
				int blockrun = block_allocation_resolution - 
				  (from % block_allocation_resolution);
				if (blockrun > run)
					blockrun = run;
				
				debug("writing partial: bit=%d, blockrun=%d (run=%d)",
				  bit, blockrun, run);
				
				DO_READ(zerobuffer, blockrun);
				
				/* This reads the buffer twice in the worst case 
				 * but we're leaning on memcmp failing early
				 * and memcpy being fast, rather than try to
				 * hand-optimized something specific.
				 */
				if (zerobuffer[0] != 0 || 
				    memcmp(zerobuffer, zerobuffer + 1, blockrun)) {
					memcpy(dst, zerobuffer, blockrun);
					bit_set(map, bit);
					dirty(client->serve, from, blockrun);
					debug("non-zero, copied and set bit %d", bit);
					/* at this point we could choose to
					 * short-cut the rest of the write for
					 * faster I/O but by continuing to do it
					 * the slow way we preserve as much 
					 * sparseness as possible.
					 */
				}
				else {
					debug("all zero, skip write");
				}
				len  -= blockrun;
				run  -= blockrun;
				from += blockrun;
			}
		}
	}
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
		if (client->block_allocation_map) {
			write_not_zeroes(
				client, 
				be64toh(request.from), 
				be32toh(request.len)
			);
		}
		else {
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
			dirty(client->serve, be64toh(request.from), be32toh(request.len));
		}
		write(client->socket, &reply, sizeof(reply));
		
		break;
	}
	return 0;
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
	
	//client_open_file(client);
	CLIENT_ERROR_ON_FAILURE(
		open_and_mmap(
			client->filename,
			&client->fileno,
			&client->size, 
			(void**) &client->mapped
		),
		"Couldn't open/mmap file %s", client->filename
	);
	client_send_hello(client);
	
	while (client_serve_request(client) == 0)
		;
		
	CLIENT_ERROR_ON_FAILURE(
		close(client->socket),
		"Couldn't close socket %d", 
		client->socket
	);
	

	close(client->socket);
	close(client->fileno);
	munmap(client->mapped, client->size);
	
	free(client);
	return NULL;
}

void control_acl(struct control_params* client)
{
	int acl_entries = 0, parsed;
	char** s_acl_entry = NULL;
	struct ip_and_mask (*acl)[], (*old_acl)[];
	
	while (1) {
		char entry[64];
		int result = read_until_newline(client->socket, entry, 64);
		if (result == -1)
			goto done;
		if (result == 1) /* blank line terminates */
			break;
		s_acl_entry = xrealloc(
			s_acl_entry, 
			++acl_entries * sizeof(struct s_acl_entry*)
		);
		s_acl_entry[acl_entries-1] = strdup(entry);
		debug("acl_entry = '%s'", s_acl_entry[acl_entries-1]);
	}
	
	parsed = parse_acl(&acl, acl_entries, s_acl_entry);
	if (parsed != acl_entries) {
		write(client->socket, "error: ", 7);
		write(client->socket, s_acl_entry[parsed], 
		  strlen(s_acl_entry[parsed]));
		write(client->socket, "\n", 1);
		free(acl);
	}
	else {
		old_acl = client->serve->acl;
		client->serve->acl = acl;
		client->serve->acl_entries = acl_entries;
		free(old_acl);
		write(client->socket, "ok\n", 3);
	}
	
done:	if (acl_entries > 0) {
		int i;
		for (i=0; i<acl_entries; i++)
			free(s_acl_entry[i]);
		free(s_acl_entry);
	}
	return;
}

static const int longest_run = 8<<20;

void* mirror_runner(void* serve_params_uncast)
{
	struct mode_serve_params *serve = (struct mode_serve_params*) serve_params_uncast;
	
	int pass;
	struct bitset_mapping *map = serve->mirror->dirty_map;
	
	for (pass=0; pass < 7 /* biblical */; pass++) {
		uint64_t current = 0;
		
		while (current < serve->size) {
			int run = bitset_run_count(map, current, longest_run);
			
			if (bitset_is_set_at(map, current)) {
				/* dirty area */
				socket_nbd_write(
					serve->mirror->client, 
					current,
					run,
					0,
					serve->mirror->mapped + current
				);
				
				bitset_clear_range(map, current, run);
			}
			current += run;
		}
	}
	
	return NULL;
}

void control_mirror(struct control_params* client)
{
	off64_t size;
	int fd, map_fd;
	struct mirror_status *mirror;
	union mysockaddr connect_to;
	char s_ip_address[64], s_port[8];
	
	CLIENT_ERROR_ON_FAILURE(
		read_until_newline(client->socket, s_ip_address, 64),
		"Failed to read destination IP"
	);
	CLIENT_ERROR_ON_FAILURE(
		read_until_newline(client->socket, s_port, 8),
		"Failed to read destination port"
	);
		
	if (parse_ip_to_sockaddr(&connect_to.generic, s_ip_address) == 0)
		CLIENT_ERROR("Couldn't parse connection address '%s'", 
		s_ip_address);
	
	connect_to.v4.sin_port = atoi(s_port);
	if (connect_to.v4.sin_port < 0 || connect_to.v4.sin_port > 65535)
		CLIENT_ERROR("Port number must be >= 0 and <= 65535");
	connect_to.v4.sin_port = htobe16(connect_to.v4.sin_port);
	
	fd = socket_connect(&connect_to.generic); /* FIXME uses wrong error handler */
	
	mirror = xmalloc(sizeof(struct mirror_status));
	mirror->client = fd;
	mirror->max_bytes_per_second = 0;
	
	CLIENT_ERROR_ON_FAILURE(
		open_and_mmap(
			client->serve->filename, 
			&map_fd,
			&size, 
			(void**) &mirror->mapped
		),
		"Failed to open and mmap %s",
		client->serve->filename
	);
	
	mirror->dirty_map = bitset_alloc(size, block_allocation_resolution);
	bitset_set_range(mirror->dirty_map, 0, size);
	
	client->serve->mirror = mirror;
	
	CLIENT_ERROR_ON_FAILURE( /* FIXME should free mirror on error */
		pthread_create(
			&mirror->thread, 
			NULL, 
			mirror_runner, 
			client->serve
		),
		"Failed to create mirror thread"
	);
}

void control_status(struct control_params* client)
{
}

void* control_serve(void* client_uncast)
{
	const int max = 256;
	char command[max];
	struct control_params* client = (struct control_params*) client_uncast;
	
	while (1) {
		CLIENT_ERROR_ON_FAILURE(
			read_until_newline(client->socket, command, max),
			"Error reading command"
		);
		
		if (strcmp(command, "acl") == 0)
			control_acl(client);
		else if (strcmp(command, "mirror") == 0)
			control_mirror(client);
		else if (strcmp(command, "status") == 0)
			control_status(client);
		else {
			write(client->socket, "error: unknown command\n", 23);
			break;
		}
	}
	
	close(client->socket);
	free(client);
	return NULL;
}

static int testmasks[9] = { 0,128,192,224,240,248,252,254,255 };

int is_included_in_acl(int list_length, struct ip_and_mask (*list)[], struct sockaddr* test)
{
	int i;
	
	for (i=0; i < list_length; i++) {
		struct ip_and_mask *entry = &(*list)[i];
		int testbits;
		char *raw_address1, *raw_address2;
		
		debug("checking acl entry %d (%d/%d)", i, test->sa_family, entry->ip.family);
		
		if (test->sa_family != entry->ip.family)
			continue;
		
		if (test->sa_family == AF_INET) {
			debug("it's an AF_INET");
			raw_address1 = (char*) 
			  &((struct sockaddr_in*) test)->sin_addr;
			raw_address2 = (char*) &entry->ip.v4.sin_addr;
		}
		else if (test->sa_family == AF_INET6) {
			debug("it's an AF_INET6");
			raw_address1 = (char*) 
			  &((struct sockaddr_in6*) test)->sin6_addr;
			raw_address2 = (char*) &entry->ip.v6.sin6_addr;
		}
		
		debug("testbits=%d", entry->mask);
		
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

void serve_open_server_socket(struct mode_serve_params* params)
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

void serve_open_control_socket(struct mode_serve_params* params)
{
	struct sockaddr_un bind_address;
	
	if (!params->control_socket_name)
		return;

	params->control = socket(AF_UNIX, SOCK_STREAM, 0);
	SERVER_ERROR_ON_FAILURE(params->control,
	  "Couldn't create control socket");
	
	memset(&bind_address, 0, sizeof(bind_address));
	bind_address.sun_family = AF_UNIX;
	strcpy(bind_address.sun_path, params->control_socket_name);
	
	unlink(params->control_socket_name); /* ignore failure */
	
	SERVER_ERROR_ON_FAILURE(
		bind(params->control, &bind_address, sizeof(bind_address)),
		"Couldn't bind control socket to %s",
		params->control_socket_name
	);
	
	SERVER_ERROR_ON_FAILURE(
		listen(params->control, 5),
		"Couldn't listen on control socket"
	);
}

void accept_nbd_client(struct mode_serve_params* params, int client_fd, struct sockaddr* client_address)
{
	pthread_t client_thread;
	struct client_params* client_params;
	
	if (params->acl && 
	    !is_included_in_acl(params->acl_entries, params->acl, client_address)) {
		write(client_fd, "Access control error", 20);
		close(client_fd);
		return;
	}
	
	client_params = xmalloc(sizeof(struct client_params));
	client_params->socket = client_fd;
	client_params->filename = params->filename;
	client_params->block_allocation_map = 
	  params->block_allocation_map;
	client_params->serve = params;
	
	SERVER_ERROR_ON_FAILURE(
		pthread_create(
			&client_thread, 
			NULL, 
			client_serve, 
			client_params
		),
		"Failed to create client thread"
	);
	/* FIXME: keep track of them? */
	/* FIXME: maybe shouldn't be fatal? */
}

void accept_control_connection(struct mode_serve_params* params, int client_fd, struct sockaddr* client_address)
{
	pthread_t control_thread;
	struct control_params* control_params;
	
	control_params = xmalloc(sizeof(struct control_params));
	control_params->socket = client_fd;
	control_params->serve = params;

	SERVER_ERROR_ON_FAILURE(
		pthread_create(
			&control_thread, 
			NULL, 
			control_serve, 
			control_params
		),
		"Failed to create client thread"
	);
}

void serve_accept_loop(struct mode_serve_params* params) 
{
	while (1) {
		int             activity_fd, client_fd;
		struct sockaddr client_address;
		fd_set          fds;
		socklen_t       socklen=sizeof(client_address);
		
		FD_ZERO(&fds);
		FD_SET(params->server, &fds);
		if (params->control_socket_name)
			FD_SET(params->control, &fds);
		
		SERVER_ERROR_ON_FAILURE(
			select(FD_SETSIZE, &fds, NULL, NULL, NULL),
			"select() failed"
		);
		
		activity_fd = FD_ISSET(params->server, &fds) ? params->server : 
		  params->control;
		client_fd = accept(activity_fd, &client_address, &socklen);
		SERVER_ERROR_ON_FAILURE(client_fd, "accept() failed");
		
		if (activity_fd == params->server)
			accept_nbd_client(params, client_fd, &client_address);
		if (activity_fd == params->control)
			accept_control_connection(params, client_fd, &client_address);
	}
}

void serve_init_allocation_map(struct mode_serve_params* params)
{
	int fd = open(params->filename, O_RDONLY);
	off64_t size;
	SERVER_ERROR_ON_FAILURE(fd, "Couldn't open %s", params->filename);
	size = lseek64(fd, 0, SEEK_END);
	params->size = size;
	SERVER_ERROR_ON_FAILURE(size, "Couldn't find size of %s", 
	  params->filename);
	params->block_allocation_map = 
		build_allocation_map(fd, size, block_allocation_resolution);
	close(fd);
}

void do_serve(struct mode_serve_params* params)
{
	serve_open_server_socket(params);
	serve_open_control_socket(params);
	serve_init_allocation_map(params);
	serve_accept_loop(params);
}

