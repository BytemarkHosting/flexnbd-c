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
#include "acl.h"

#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <unistd.h>

struct mirror_status * mirror_status_create(
		struct server * serve, 
		int fd,
		int max_Bps,
		int action_at_finish)
{
	/* FIXME: shouldn't map_fd get closed? */
	int map_fd;
	off64_t size;
	struct mirror_status * mirror;

	NULLCHECK( serve );

	mirror = xmalloc(sizeof(struct mirror_status));
	mirror->client = fd;
	mirror->max_bytes_per_second = max_Bps;
	mirror->action_at_finish = action_at_finish;
	
	FATAL_IF_NEGATIVE(
		open_and_mmap(
			serve->filename, 
			&map_fd,
			&size, 
			(void**) &mirror->mapped
		),
		"Failed to open and mmap %s",
		serve->filename
	);
	
	mirror->dirty_map = bitset_alloc(size, 4096);
	bitset_set_range(mirror->dirty_map, 0, size);

	return mirror;
}


void mirror_status_destroy( struct mirror_status *mirror )
{
	NULLCHECK( mirror );
	close(mirror->client);
	free(mirror->dirty_map);
	free(mirror);
}


/** The mirror code will split NBD writes, making them this long as a maximum */
static const int mirror_longest_write = 8<<20;

/** If, during a mirror pass, we have sent this number of bytes or fewer, we
 *  go to freeze the I/O and finish it off.  This is just a guess.
 */
static const unsigned int mirror_last_pass_after_bytes_written = 100<<20;

/** The largest number of full passes we'll do - the last one will always 
 *  cause the I/O to freeze, however many bytes are left to copy.
 */
static const int mirror_maximum_passes = 7;

/* A single mirror pass over the disc, optionally locking IO around the
 * transfer.
 */
int mirror_pass(struct server * serve, int should_lock, uint64_t *written)
{
	uint64_t current = 0;
	int success = 1;
	struct bitset_mapping *map = serve->mirror->dirty_map;
	*written = 0;

	while (current < serve->size) {
		int run = bitset_run_count(map, current, mirror_longest_write);

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
			if (should_lock) { server_lock_io( serve ); }
			{
				/** FIXME: do something useful with bytes/second */

				/** FIXME: error handling code here won't unlock */
				socket_nbd_write( serve->mirror->client, 
						current,
						run,
						0,
						serve->mirror->mapped + current);

				/* now mark it clean */
				bitset_clear_range(map, current, run);
			}
			if (should_lock) { server_unlock_io( serve ); }

			*written += run;
		}
		current += run;

		if (serve->mirror->signal_abandon) {
			success = 0;
			break;
		}
	}

	return success;
}


void mirror_on_exit( struct server * serve )
{
	serve_signal_close( serve );
	/* We have to wait until the server is closed before unlocking
	 * IO.  This is because the client threads check to see if the
	 * server is still open before reading or writing inside their
	 * own locks. If we don't wait for the close, there's no way to
	 * guarantee the server thread will win the race and we risk the
	 * clients seeing a "successful" write to a dead disc image.
	 */
	serve_wait_for_close( serve );
}

/** Thread launched to drive mirror process */
void* mirror_runner(void* serve_params_uncast)
{
	int pass;
	struct server *serve = (struct server*) serve_params_uncast;
	uint64_t written;

	NULLCHECK( serve );
	NULLCHECK( serve->mirror );
	NULLCHECK( serve->mirror->dirty_map );

	debug("Starting mirror" );
	
	for (pass=0; pass < mirror_maximum_passes-1; pass++) {
		debug("mirror start pass=%d", pass);
		
		if ( !mirror_pass( serve, 1, &written ) ){
			goto abandon_mirror;
		}
		
		/* if we've not written anything */
		if (written < mirror_last_pass_after_bytes_written) { break; }
	}

	server_lock_io( serve );
	{
		if ( mirror_pass( serve, 0, &written ) &&
				ACTION_EXIT == serve->mirror->action_at_finish) {
			debug("exit!");
			mirror_on_exit( serve );
			info("Server closed, quitting "
					"after successful migration");
		} 
	}
	server_unlock_io( serve );

abandon_mirror:
	mirror_status_destroy( serve->mirror );
	serve->mirror = NULL; /* and we're gone */
	
	return NULL;
}


#define write_socket(msg) write(client->socket, (msg "\n"), strlen((msg))+1)

/** Command parser to start mirror process from socket input */
int control_mirror(struct control_params* client, int linesc, char** lines)
{
	NULLCHECK( client );

	off64_t remote_size;
	struct server * serve = client->serve;
	int fd;
	struct mirror_status *mirror;
	union mysockaddr connect_to;
	union mysockaddr connect_from;
	int use_connect_from = 0;
	uint64_t max_Bps;
	int action_at_finish;
	int raw_port;
	
	
	if (linesc < 2) {
		write_socket("1: mirror takes at least two parameters");
		return -1;
	}

	if (parse_ip_to_sockaddr(&connect_to.generic, lines[0]) == 0) {
		write_socket("1: bad IP address");
		return -1;
	}
	
	raw_port = atoi(lines[1]);
	if (raw_port < 0 || raw_port > 65535) {
		write_socket("1: bad IP port number");
		return -1;
	}
	connect_to.v4.sin_port = htobe16(raw_port);

	if (linesc > 2) {
		if (parse_ip_to_sockaddr(&connect_from.generic, lines[2]) == 0) {
			write_socket("1: bad bind address");
			return -1;
		}
		use_connect_from = 1;
	}

	max_Bps = 0;
	if (linesc > 3) {
		max_Bps = atoi(lines[2]);
	}
	
	action_at_finish = ACTION_EXIT;
	if (linesc > 4) {
		if (strcmp("exit", lines[3]) == 0) {
			action_at_finish = ACTION_EXIT;
		}
		else if (strcmp("nothing", lines[3]) == 0) {
			action_at_finish = ACTION_NOTHING;
		}
		else {
			write_socket("1: action must be 'exit' or 'nothing'");
			return -1;
		}
	}
	
	if (linesc > 5) {
		write_socket("1: unrecognised parameters to mirror");
		return -1;
	}

	/** I don't like use_connect_from but socket_connect doesn't take *mysockaddr :( */
	struct sockaddr *afrom = use_connect_from ? &connect_from.generic : NULL;
	fd = socket_connect(&connect_to.generic, afrom);
	
	remote_size = socket_nbd_read_hello(fd);
	if( remote_size != (off64_t)serve->size ){
		warn("Remote size (%d) doesn't match local (%d)", remote_size, serve->size );
		write_socket( "1: remote size (%d) doesn't match local (%d)");
		close(fd);
		return -1;
	}
	
	mirror = mirror_status_create( serve,
			fd, 
			max_Bps ,
			action_at_finish );
	serve->mirror = mirror;
	
	FATAL_IF( /* FIXME should free mirror on error */
		0 != pthread_create(
			&mirror->thread, 
			NULL, 
			mirror_runner, 
			serve
		),
		"Failed to create mirror thread"
	);
	
	write_socket("0: mirror started");
	
	return 0;
}

/** Command parser to alter access control list from socket input */
int control_acl(struct control_params* client, int linesc, char** lines)
{
	NULLCHECK( client );

	struct acl * old_acl = client->serve->acl;
	struct acl * new_acl = acl_create( linesc, lines, old_acl ? old_acl->default_deny : 0 );
	
	if (new_acl->len != linesc) {
		write(client->socket, "1: bad spec: ", 13);
		write(client->socket, lines[new_acl->len], 
		  strlen(lines[new_acl->len]));
		write(client->socket, "\n", 1);
		acl_destroy( new_acl );
	}
	else {
		server_replace_acl( client->serve, new_acl );
		write_socket("0: updated");
	}
	
	return 0;
}

/** FIXME: add some useful statistics */
int control_status(
		struct control_params* client __attribute__ ((unused)), 
		int linesc __attribute__ ((unused)), 
		char** lines __attribute__((unused))
		)
{
	return 0;
}

void control_cleanup(struct control_params* client, 
		int fatal __attribute__ ((unused)) )
{
	if (client->socket) { close(client->socket); }
	free(client);
}

/** Master command parser for control socket connections, delegates quickly */
void* control_serve(void* client_uncast)
{
	struct control_params* client = (struct control_params*) client_uncast;
	char **lines = NULL;
	int finished=0;
	
	error_set_handler((cleanup_handler*) control_cleanup, client);
		
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
			if (control_acl(client, linesc-1, lines+1) < 0) {
				finished = 1;
			}
		}
		else if (strcmp(lines[0], "mirror") == 0) {
			if (control_mirror(client, linesc-1, lines+1) < 0) {
				finished = 1;
			}
		}
		else if (strcmp(lines[0], "status") == 0) {
			if (control_status(client, linesc-1, lines+1) < 0) {
				finished = 1;
			}
		}
		else {
			write(client->socket, "10: unknown command\n", 23);
			finished = 1;
		}
		
		for (i=0; i<linesc; i++) {
			free(lines[i]);
		}
		free(lines);
	}
	
	control_cleanup(client, 0);
	
	return NULL;
}

void accept_control_connection(struct server* params, int client_fd, 
		union mysockaddr* client_address __attribute__ ((unused)) )
{
	pthread_t control_thread;
	struct control_params* control_params;
	
	control_params = xmalloc(sizeof(struct control_params));
	control_params->socket = client_fd;
	control_params->serve = params;

	FATAL_IF(
		0 != pthread_create(
			&control_thread, 
			NULL, 
			control_serve, 
			control_params
		),
		"Failed to create client thread"
	);

	/* FIXME: This thread *really* shouldn't detach
	 * Since it can see the server object, if listen switches mode
	 * while this is live, Bad Things Could Happen.
	 */
	pthread_detach( control_thread );
}

void serve_open_control_socket(struct server* params)
{
	struct sockaddr_un bind_address;
	
	if (!params->control_socket_name) { return; }

	params->control_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	FATAL_IF_NEGATIVE(params->control_fd ,
	  "Couldn't create control socket");
	
	memset(&bind_address, 0, sizeof(bind_address));
	bind_address.sun_family = AF_UNIX;
	strncpy(bind_address.sun_path, params->control_socket_name, sizeof(bind_address.sun_path)-1);
	
	unlink(params->control_socket_name); /* ignore failure */
	
	FATAL_IF_NEGATIVE(
		bind(params->control_fd , &bind_address, sizeof(bind_address)),
		"Couldn't bind control socket to %s",
		params->control_socket_name
	);
	
	FATAL_IF_NEGATIVE(
		listen(params->control_fd , 5),
		"Couldn't listen on control socket"
	);
}

