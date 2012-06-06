/*  FlexNBD server (C) Bytemark Hosting 2012

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/** The control server responds on a UNIX socket and services our "remote"
 *  commands which are used for changing the access control list, initiating
 *  a mirror process, or asking for status.  The protocol is pretty simple -
 *  after connecting the client sends a series of LF-terminated lines, followed
 *  by a blank line (i.e. double LF).  The first line is taken to be the command
 *  name to invoke, and the lines before the double LF are its arguments.
 *
 *  These commands can be invoked remotely from the command line, with the 
 *  client code to be found in remote.c
 */

#include "serve.h"
#include "util.h"
#include "ioutil.h"
#include "parse.h"
#include "readwrite.h"
#include "bitset.h"
#include "self_pipe.h"

#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <unistd.h>

/** The mirror code will split NBD writes, making them this long as a maximum */
static const int mirror_longest_write = 8<<20;

/** If, during a mirror pass, we have sent this number of bytes or fewer, we
 *  go to freeze the I/O and finish it off.  This is just a guess.
 */
static const int mirror_last_pass_after_bytes_written = 100<<20;

/** The largest number of full passes we'll do - the last one will always 
 *  cause the I/O to freeze, however many bytes are left to copy.
 */
static const int mirror_maximum_passes = 7;

/** Thread launched to drive mirror process */
void* mirror_runner(void* serve_params_uncast)
{
	const int last_pass = mirror_maximum_passes-1;
	int pass;
	struct server *serve = (struct server*) serve_params_uncast;
	struct bitset_mapping *map = serve->mirror->dirty_map;
	
	for (pass=0; pass < mirror_maximum_passes; pass++) {
		uint64_t current = 0;
		uint64_t written = 0;
		
		debug("mirror start pass=%d", pass);
		
		if (pass == last_pass) {
			/* last pass, stop everything else */
			SERVER_ERROR_ON_FAILURE(
				pthread_mutex_lock(&serve->l_accept),
				"Problem with accept lock"
			);
			SERVER_ERROR_ON_FAILURE(
				pthread_mutex_lock(&serve->l_io),
				"Problem with I/O lock"
			);
		}
		
		while (current < serve->size) {
			int run;
		
			run = bitset_run_count(map, current, mirror_longest_write);
			
			debug("mirror current=%ld, run=%d", current, run);
			
			/* FIXME: we could avoid sending sparse areas of the 
			 * disc here, and probably save a lot of bandwidth and
			 * time (if we know the destination starts off zeroed).
			 */ 
			if (bitset_is_set_at(map, current)) {
				/* We've found a dirty area, send it */
				debug("^^^ writing");
				
				/* We need to stop the main thread from working 
				 * because it might corrupt the dirty map.  This
				 * is likely to slow things down but will be
				 * safe.
				 */
				if (pass < last_pass)
					SERVER_ERROR_ON_FAILURE(
						pthread_mutex_lock(&serve->l_io),
						"Problem with I/O lock"
					);
				
				/** FIXME: do something useful with bytes/second */
				
				/** FIXME: error handling code here won't unlock */
				socket_nbd_write(
					serve->mirror->client, 
					current,
					run,
					0,
					serve->mirror->mapped + current
				);
				
				/* now mark it clean */
				bitset_clear_range(map, current, run);
				
				if (pass < last_pass)
					SERVER_ERROR_ON_FAILURE(
						pthread_mutex_unlock(&serve->l_io),
						"Problem with I/O unlock"
					);
				
				written += run;
			}
			current += run;
		}
		
		/* if we've not written anything */
		if (written < mirror_last_pass_after_bytes_written)
			pass = last_pass;
	}
	
	switch (serve->mirror->action_at_finish)
	{
	case ACTION_PROXY:
		debug("proxy!");
		serve->proxy_fd = serve->mirror->client;
		/* don't close our file descriptor, we still need it! */
		break;
	case ACTION_EXIT:
		debug("exit!");
		serve_signal_close( serve );
		/* fall through */
	case ACTION_NOTHING:
		debug("nothing!");
		close(serve->mirror->client);
	}
	
	free(serve->mirror->dirty_map);
	free(serve->mirror);
	serve->mirror = NULL; /* and we're gone */

	SERVER_ERROR_ON_FAILURE(
		pthread_mutex_unlock(&serve->l_accept),
		"Problem with accept unlock"
	);
	SERVER_ERROR_ON_FAILURE(
		pthread_mutex_unlock(&serve->l_io),
		"Problem with I/O unlock"
	);
	
	return NULL;
}

#define write_socket(msg) write(client->socket, (msg "\n"), strlen((msg))+1)

/** Command parser to start mirror process from socket input */
int control_mirror(struct control_params* client, int linesc, char** lines)
{
	off64_t size, remote_size;
	int fd, map_fd;
	struct mirror_status *mirror;
	union mysockaddr connect_to;
	uint64_t max_bytes_per_second;
	int action_at_finish;
	
	if (linesc < 2) {
		write_socket("1: mirror takes at least two parameters");
		return -1;
	}
	
	if (parse_ip_to_sockaddr(&connect_to.generic, lines[0]) == 0) {
		write_socket("1: bad IP address");
		return -1;
	}
	
	connect_to.v4.sin_port = atoi(lines[1]);
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
	
	write_socket("0: mirror started");
	
	return 0;
}

/** Command parser to alter access control list from socket input */
int control_acl(struct control_params* client, int linesc, char** lines)
{
	int parsedc;
	struct ip_and_mask (*acl)[], (*old_acl)[];
	
	parsedc = parse_acl(&acl, linesc, lines);
	if (parsedc != linesc) {
		write(client->socket, "1: bad spec: ", 13);
		write(client->socket, lines[parsedc], 
		  strlen(lines[parsedc]));
		write(client->socket, "\n", 1);
		free(acl);
	}
	else {
		old_acl = client->serve->acl;
		client->serve->acl = acl;
		client->serve->acl_entries = linesc;
		free(old_acl);
		write_socket("0: updated");
	}
	
	return 0;
}

/** FIXME: add some useful statistics */
int control_status(struct control_params* client, int linesc, char** lines)
{
	return 0;
}

/** Master command parser for control socket connections, delegates quickly */
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

void accept_control_connection(struct server* params, int client_fd, union mysockaddr* client_address)
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

void serve_open_control_socket(struct server* params)
{
	struct sockaddr_un bind_address;
	
	if (!params->control_socket_name)
		return;

	params->control_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	SERVER_ERROR_ON_FAILURE(params->control_fd ,
	  "Couldn't create control socket");
	
	memset(&bind_address, 0, sizeof(bind_address));
	bind_address.sun_family = AF_UNIX;
	strncpy(bind_address.sun_path, params->control_socket_name, sizeof(bind_address.sun_path)-1);
	
	unlink(params->control_socket_name); /* ignore failure */
	
	SERVER_ERROR_ON_FAILURE(
		bind(params->control_fd , &bind_address, sizeof(bind_address)),
		"Couldn't bind control socket to %s",
		params->control_socket_name
	);
	
	SERVER_ERROR_ON_FAILURE(
		listen(params->control_fd , 5),
		"Couldn't listen on control socket"
	);
}

