#include "params.h"
#include "util.h"
#include "ioutil.h"
#include "parse.h"
#include "readwrite.h"
#include "bitset.h"

#include <stdlib.h>
#include <string.h>
#include <sys/un.h>

static const int longest_run = 8<<20;

void* mirror_runner(void* serve_params_uncast)
{
	struct mode_serve_params *serve = (struct mode_serve_params*) serve_params_uncast;
	
	int pass;
	struct bitset_mapping *map = serve->mirror->dirty_map;
	
	for (pass=0; pass < 7 /* biblical */; pass++) {
		uint64_t current = 0;
		
		debug("mirror start pass=%d", pass);
		
		while (current < serve->size) {
			int run = bitset_run_count(map, current, longest_run);
			
			debug("mirror current=%ld, run=%d", current, run);
			
			if (bitset_is_set_at(map, current)) {
				debug("^^^ writing");
				
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
	off64_t size, remote_size;
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
	
	remote_size = socket_nbd_read_hello(fd);
	remote_size = remote_size; // shush compiler
	
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
	
	mirror->dirty_map = bitset_alloc(size, 4096);
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

