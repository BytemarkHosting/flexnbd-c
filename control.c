#include "params.h"
#include "util.h"
#include "ioutil.h"
#include "parse.h"
#include "readwrite.h"
#include "bitset.h"

#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <unistd.h>

static const int longest_run = 8<<20;

void* mirror_runner(void* serve_params_uncast)
{
	struct mode_serve_params *serve = (struct mode_serve_params*) serve_params_uncast;
	
	const int max_passes = 7; /* biblical */
	int pass;
	struct bitset_mapping *map = serve->mirror->dirty_map;
	
	for (pass=0; pass < max_passes; pass++) {
		uint64_t current = 0;
		uint64_t written = 0;
		
		debug("mirror start pass=%d", pass);
		
		while (current < serve->size) {
			int run;
		
			run = bitset_run_count(map, current, longest_run);
			
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
				written += run;
			}
			current += run;
		}
		
		if (written == 0)
			pass = max_passes-1;
	}
	
	return NULL;
}

#define write_socket(msg) write(client->socket, (msg "\n"), strlen((msg))+1)

int control_mirror(struct control_params* client, int linesc, char** lines)
{
	off64_t size, remote_size;
	int fd, map_fd;
	struct mirror_status *mirror;
	union mysockaddr connect_to;
	char s_ip_address[64], s_port[8];
	uint64_t max_bytes_per_second;
	int action_at_finish;
	
	if (linesc < 2) {
		write_socket("1: mirror takes at least two parameters");
		return -1;
	}
	
	if (parse_ip_to_sockaddr(&connect_to.generic, s_ip_address) == 0) {
		write_socket("1: bad IP address");
		return -1;
	}
	
	connect_to.v4.sin_port = atoi(s_port);
	if (connect_to.v4.sin_port < 0 || connect_to.v4.sin_port > 65535) {
		write_socket("1: bad IP port number");
		return -1;
	}
	connect_to.v4.sin_port = htobe16(connect_to.v4.sin_port);
	
	max_bytes_per_second = 0;
	if (linesc > 2) {
		max_bytes_per_second = atoi(lines[2]);
	}
	
	action_at_finish = ACTION_PROXY;
	if (linesc > 3) {
		if (strcmp("proxy", lines[3]) == 0)
			action_at_finish = ACTION_PROXY;
		else if (strcmp("exit", lines[3]) == 0)
			action_at_finish = ACTION_EXIT;
		else if (strcmp("nothing", lines[3]) == 0)
			action_at_finish = ACTION_NOTHING;
		else {
			write_socket("1: action must be one of 'proxy', 'exit' or 'nothing'");
			return -1;
		}
	}
	
	if (linesc > 4) {
		write_socket("1: unrecognised parameters to mirror");
		return -1;
	}
	
	fd = socket_connect(&connect_to.generic);
	
	remote_size = socket_nbd_read_hello(fd);
	remote_size = remote_size; // shush compiler
	
	mirror = xmalloc(sizeof(struct mirror_status));
	mirror->client = fd;
	mirror->max_bytes_per_second = max_bytes_per_second;
	mirror->action_at_finish = action_at_finish;
	
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
	
	return 0;
}

int control_acl(struct control_params* client, int linesc, char** lines)
{
	int acl_entries = 0, parsed;
	char** s_acl_entry = NULL;
	struct ip_and_mask (*acl)[], (*old_acl)[];
	
	parsed = parse_acl(&acl, linesc, lines);
	if (parsed != linesc) {
		write(client->socket, "1: bad spec ", 12);
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
		write_socket("0: updated");
	}
	
	return 0;
}

int control_status(struct control_params* client, int linesc, char** lines)
{
	return 0;
}

void* control_serve(void* client_uncast)
{
	struct control_params* client = (struct control_params*) client_uncast;
	char **lines = NULL;
	int finished=0;
	
	while (!finished) {
		int i, linesc;		
		linesc = read_lines_until_blankline(client->socket, 256, &lines);
		
		if (linesc < 1)
		{
			write(client->socket, "9: missing command\n", 19);
			finished = 1;
			/* ignore failure */
		}
		else if (strcmp(lines[0], "acl") == 0) {
			if (control_acl(client, linesc-1, lines+1) < 0)
				finished = 1;
		}
		else if (strcmp(lines[0], "mirror") == 0) {
			if (control_mirror(client, linesc-1, lines+1) < 0)
				finished = 1;
		}
		else if (strcmp(lines[0], "status") == 0) {
			if (control_status(client, linesc-1, lines+1) < 0)
				finished = 1;
		}
		else {
			write(client->socket, "10: unknown command\n", 23);
			finished = 1;
		}
		
		for (i=0; i<linesc; i++)
			free(lines[i]);
		free(lines);
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

