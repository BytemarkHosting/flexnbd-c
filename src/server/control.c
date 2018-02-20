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

#include "control.h"
#include "mirror.h"
#include "serve.h"
#include "util.h"
#include "ioutil.h"
#include "parse.h"
#include "readwrite.h"
#include "bitset.h"
#include "self_pipe.h"
#include "acl.h"
#include "status.h"
#include "mbox.h"

#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <unistd.h>


struct control *control_create(struct flexnbd *flexnbd, const char *csn)
{
    struct control *control = xmalloc(sizeof(struct control));

    NULLCHECK(csn);

    control->flexnbd = flexnbd;
    control->socket_name = csn;
    control->open_signal = self_pipe_create();
    control->close_signal = self_pipe_create();
    control->mirror_state_mbox = mbox_create();

    return control;
}


void control_signal_close(struct control *control)
{

    NULLCHECK(control);
    self_pipe_signal(control->close_signal);
}


void control_destroy(struct control *control)
{
    NULLCHECK(control);

    mbox_destroy(control->mirror_state_mbox);
    self_pipe_destroy(control->close_signal);
    self_pipe_destroy(control->open_signal);
    free(control);
}

struct control_client *control_client_create(struct flexnbd *flexnbd,
					     int client_fd,
					     struct mbox *state_mbox)
{
    NULLCHECK(flexnbd);

    struct control_client *control_client =
	xmalloc(sizeof(struct control_client));

    control_client->socket = client_fd;
    control_client->flexnbd = flexnbd;
    control_client->mirror_state_mbox = state_mbox;
    return control_client;
}



void control_client_destroy(struct control_client *client)
{
    NULLCHECK(client);
    free(client);
}


void control_respond(struct control_client *client);

void control_handle_client(struct control *control, int client_fd)
{
    NULLCHECK(control);
    NULLCHECK(control->flexnbd);
    struct control_client *control_client =
	control_client_create(control->flexnbd,
			      client_fd,
			      control->mirror_state_mbox);

    /* We intentionally don't spawn a thread for the client here.
     * This is to avoid having more than one thread potentially
     * waiting on the migration commit status.
     */
    control_respond(control_client);
}


void control_accept_client(struct control *control)
{

    int client_fd;
    union mysockaddr client_address;
    socklen_t addrlen = sizeof(union mysockaddr);

    client_fd =
	accept(control->control_fd, &client_address.generic, &addrlen);
    FATAL_IF(-1 == client_fd, "control accept failed");

    control_handle_client(control, client_fd);
}

int control_accept(struct control *control)
{
    NULLCHECK(control);

    fd_set fds;

    FD_ZERO(&fds);
    FD_SET(control->control_fd, &fds);
    self_pipe_fd_set(control->close_signal, &fds);
    debug("Control thread selecting");
    FATAL_UNLESS(0 < select(FD_SETSIZE, &fds, NULL, NULL, NULL),
		 "Control select failed.");

    if (self_pipe_fd_isset(control->close_signal, &fds)) {
	return 0;
    }

    if (FD_ISSET(control->control_fd, &fds)) {
	control_accept_client(control);
    }
    return 1;
}


void control_accept_loop(struct control *control)
{
    while (control_accept(control));
}


int open_control_socket(const char *socket_name)
{
    struct sockaddr_un bind_address;
    int control_fd;

    if (!socket_name) {
	fatal("Tried to open a control socket without a socket name");
    }

    control_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    FATAL_IF_NEGATIVE(control_fd, "Couldn't create control socket");

    memset(&bind_address, 0, sizeof(struct sockaddr_un));
    bind_address.sun_family = AF_UNIX;
    strncpy(bind_address.sun_path, socket_name,
	    sizeof(bind_address.sun_path) - 1);

    //unlink(socket_name); /* ignore failure */

    FATAL_IF_NEGATIVE(bind
		      (control_fd, &bind_address, sizeof(bind_address)),
		      "Couldn't bind control socket to %s: %s",
		      socket_name, strerror(errno)
	);

    FATAL_IF_NEGATIVE(listen(control_fd, 5),
		      "Couldn't listen on control socket");
    return control_fd;
}


void control_listen(struct control *control)
{
    NULLCHECK(control);
    control->control_fd = open_control_socket(control->socket_name);
}

void control_wait_for_open_signal(struct control *control)
{
    fd_set fds;
    FD_ZERO(&fds);
    self_pipe_fd_set(control->open_signal, &fds);
    FATAL_IF_NEGATIVE(select(FD_SETSIZE, &fds, NULL, NULL, NULL),
		      "select() failed");

    self_pipe_signal_clear(control->open_signal);
}


void control_serve(struct control *control)
{
    NULLCHECK(control);

    control_wait_for_open_signal(control);
    control_listen(control);
    while (control_accept(control));
}


void control_cleanup(struct control *control,
		     int fatal __attribute__ ((unused)))
{
    NULLCHECK(control);
    unlink(control->socket_name);
    close(control->control_fd);
}


void *control_runner(void *control_uncast)
{
    debug("Control thread");
    NULLCHECK(control_uncast);
    struct control *control = (struct control *) control_uncast;

    error_set_handler((cleanup_handler *) control_cleanup, control);

    control_serve(control);

    control_cleanup(control, 0);
    pthread_exit(NULL);
}


#define write_socket(msg) write(client_fd, (msg "\n"), strlen((msg))+1)

void control_write_mirror_response(enum mirror_state mirror_state,
				   int client_fd)
{
    switch (mirror_state) {
    case MS_INIT:
    case MS_UNKNOWN:
	write_socket("1: Mirror failed to initialise");
	fatal("Impossible mirror state: %d", mirror_state);
    case MS_FAIL_CONNECT:
	write_socket("1: Mirror failed to connect");
	break;
    case MS_FAIL_REJECTED:
	write_socket("1: Mirror was rejected");
	break;
    case MS_FAIL_NO_HELLO:
	write_socket("1: Remote server failed to respond");
	break;
    case MS_FAIL_SIZE_MISMATCH:
	write_socket("1: Remote size does not match local size");
	break;
    case MS_ABANDONED:
	write_socket("1: Mirroring abandoned");
	break;
    case MS_GO:
    case MS_DONE:		/* Yes, I know we know better, but it's simpler this way */
	write_socket("0: Mirror started");
	break;
    default:
	fatal("Unhandled mirror state: %d", mirror_state);
    }
}

#undef write_socket


/* Call this in the thread where you want to receive the mirror state */
enum mirror_state control_client_mirror_wait(struct control_client *client)
{
    NULLCHECK(client);
    NULLCHECK(client->mirror_state_mbox);

    struct mbox *mbox = client->mirror_state_mbox;
    enum mirror_state mirror_state;
    enum mirror_state *contents;

    contents = (enum mirror_state *) mbox_receive(mbox);
    NULLCHECK(contents);

    mirror_state = *contents;

    free(contents);

    return mirror_state;
}

#define write_socket(msg) write(client->socket, (msg "\n"), strlen((msg))+1)
/** Command parser to start mirror process from socket input */
int control_mirror(struct control_client *client, int linesc, char **lines)
{
    NULLCHECK(client);

    struct flexnbd *flexnbd = client->flexnbd;
    union mysockaddr *connect_to = xmalloc(sizeof(union mysockaddr));
    union mysockaddr *connect_from = NULL;
    uint64_t max_Bps = UINT64_MAX;
    int action_at_finish;
    int raw_port;


    if (linesc < 2) {
	write_socket("1: mirror takes at least two parameters");
	return -1;
    }

    if (parse_ip_to_sockaddr(&connect_to->generic, lines[0]) == 0) {
	write_socket("1: bad IP address");
	return -1;
    }

    raw_port = atoi(lines[1]);
    if (raw_port < 0 || raw_port > 65535) {
	write_socket("1: bad IP port number");
	return -1;
    }
    connect_to->v4.sin_port = htobe16(raw_port);

    action_at_finish = ACTION_EXIT;
    if (linesc > 2) {
	if (strcmp("exit", lines[2]) == 0) {
	    action_at_finish = ACTION_EXIT;
	} else if (strcmp("unlink", lines[2]) == 0) {
	    action_at_finish = ACTION_UNLINK;
	} else if (strcmp("nothing", lines[2]) == 0) {
	    action_at_finish = ACTION_NOTHING;
	} else {
	    write_socket("1: action must be 'exit' or 'nothing'");
	    return -1;
	}
    }

    if (linesc > 3) {
	connect_from = xmalloc(sizeof(union mysockaddr));
	if (parse_ip_to_sockaddr(&connect_from->generic, lines[3]) == 0) {
	    write_socket("1: bad bind address");
	    return -1;
	}
    }

    if (linesc > 4) {
	errno = 0;
	max_Bps = strtoull(lines[4], NULL, 10);
	if (errno == ERANGE) {
	    write_socket("1: max_bps out of range");
	    return -1;
	} else if (errno != 0) {
	    write_socket("1: max_bps couldn't be parsed");
	    return -1;
	}
    }


    if (linesc > 5) {
	write_socket("1: unrecognised parameters to mirror");
	return -1;
    }

    struct server *serve = flexnbd_server(flexnbd);

    server_lock_start_mirror(serve);
    {
	if (server_mirror_can_start(serve)) {
	    serve->mirror_super = mirror_super_create(serve->filename,
						      connect_to,
						      connect_from,
						      max_Bps,
						      action_at_finish,
						      client->
						      mirror_state_mbox);
	    serve->mirror = serve->mirror_super->mirror;
	    server_prevent_mirror_start(serve);
	} else {
	    if (serve->mirror_super) {
		warn("Tried to start a second mirror run");
		write_socket("1: mirror already running");
	    } else {
		warn("Cannot start mirroring, shutting down");
		write_socket("1: shutting down");
	    }
	}

    }
    server_unlock_start_mirror(serve);

    /* Do this outside the lock to minimise the length of time the
     * sighandler can block the serve thread
     */
    if (serve->mirror_super) {
	FATAL_IF(0 != pthread_create(&serve->mirror_super->thread,
				     NULL,
				     mirror_super_runner,
				     serve),
		 "Failed to create mirror thread");

	debug("Control thread mirror super waiting");
	enum mirror_state state = control_client_mirror_wait(client);
	debug("Control thread writing response");
	control_write_mirror_response(state, client->socket);
    }

    debug("Control thread going away.");

    return 0;
}

int control_mirror_max_bps(struct control_client *client, int linesc,
			   char **lines)
{
    NULLCHECK(client);
    NULLCHECK(client->flexnbd);

    struct server *serve = flexnbd_server(client->flexnbd);
    uint64_t max_Bps;

    if (!serve->mirror_super) {
	write_socket("1: Not currently mirroring");
	return -1;
    }

    if (linesc != 1) {
	write_socket("1: Bad format");
	return -1;
    }

    errno = 0;
    max_Bps = strtoull(lines[0], NULL, 10);
    if (errno == ERANGE) {
	write_socket("1: max_bps out of range");
	return -1;
    } else if (errno != 0) {
	write_socket("1: max_bps couldn't be parsed");
	return -1;
    }

    serve->mirror->max_bytes_per_second = max_Bps;
    write_socket("0: updated");

    return 0;
}

#undef write_socket

/** Command parser to alter access control list from socket input */
int control_acl(struct control_client *client, int linesc, char **lines)
{
    NULLCHECK(client);
    NULLCHECK(client->flexnbd);
    struct flexnbd *flexnbd = client->flexnbd;

    int default_deny = flexnbd_default_deny(flexnbd);
    struct acl *new_acl = acl_create(linesc, lines, default_deny);

    if (new_acl->len != linesc) {
	warn("Bad ACL spec: %s", lines[new_acl->len]);
	write(client->socket, "1: bad spec: ", 13);
	write(client->socket, lines[new_acl->len],
	      strlen(lines[new_acl->len]));
	write(client->socket, "\n", 1);
	acl_destroy(new_acl);
    } else {
	flexnbd_replace_acl(flexnbd, new_acl);
	info("ACL set");
	write(client->socket, "0: updated\n", 11);
    }

    return 0;
}


int control_break(struct control_client *client,
		  int linesc __attribute__ ((unused)),
		  char **lines __attribute__ ((unused))
    )
{
    NULLCHECK(client);
    NULLCHECK(client->flexnbd);

    int result = 0;
    struct flexnbd *flexnbd = client->flexnbd;

    struct server *serve = flexnbd_server(flexnbd);

    server_lock_start_mirror(serve);
    {
	if (server_is_mirroring(serve)) {

	    info("Signaling to abandon mirror");
	    server_abandon_mirror(serve);
	    debug("Abandon signaled");

	    if (server_is_closed(serve)) {
		info("Mirror completed while canceling");
		write(client->socket, "1: mirror completed\n", 20);
	    } else {
		info("Mirror successfully stopped.");
		write(client->socket, "0: mirror stopped\n", 18);
		result = 1;
	    }

	} else {
	    warn("Not mirroring.");
	    write(client->socket, "1: not mirroring\n", 17);
	}
    }
    server_unlock_start_mirror(serve);

    return result;
}


/** FIXME: add some useful statistics */
int control_status(struct control_client *client,
		   int linesc __attribute__ ((unused)),
		   char **lines __attribute__ ((unused))
    )
{
    NULLCHECK(client);
    NULLCHECK(client->flexnbd);
    struct status *status = flexnbd_status_create(client->flexnbd);

    write(client->socket, "0: ", 3);
    status_write(status, client->socket);
    status_destroy(status);

    return 0;
}

void control_client_cleanup(struct control_client *client,
			    int fatal __attribute__ ((unused)))
{
    if (client->socket) {
	close(client->socket);
    }

    /* This is wrongness */
    if (server_acl_locked(client->flexnbd->serve)) {
	server_unlock_acl(client->flexnbd->serve);
    }

    control_client_destroy(client);
}

/** Master command parser for control socket connections, delegates quickly */
void control_respond(struct control_client *client)
{
    char **lines = NULL;

    error_set_handler((cleanup_handler *) control_client_cleanup, client);

    int i, linesc;
    linesc = read_lines_until_blankline(client->socket, 256, &lines);

    if (linesc < 1) {
	write(client->socket, "9: missing command\n", 19);
	/* ignore failure */
    } else if (strcmp(lines[0], "acl") == 0) {
	info("acl command received");
	if (control_acl(client, linesc - 1, lines + 1) < 0) {
	    debug("acl command failed");
	}
    } else if (strcmp(lines[0], "mirror") == 0) {
	info("mirror command received");
	if (control_mirror(client, linesc - 1, lines + 1) < 0) {
	    debug("mirror command failed");
	}
    } else if (strcmp(lines[0], "break") == 0) {
	info("break command received");
	if (control_break(client, linesc - 1, lines + 1) < 0) {
	    debug("break command failed");
	}
    } else if (strcmp(lines[0], "status") == 0) {
	info("status command received");
	if (control_status(client, linesc - 1, lines + 1) < 0) {
	    debug("status command failed");
	}
    } else if (strcmp(lines[0], "mirror_max_bps") == 0) {
	info("mirror_max_bps command received");
	if (control_mirror_max_bps(client, linesc - 1, lines + 1) < 0) {
	    debug("mirror_max_bps command failed");
	}
    } else {
	write(client->socket, "10: unknown command\n", 23);
    }

    for (i = 0; i < linesc; i++) {
	free(lines[i]);
    }
    free(lines);

    control_client_cleanup(client, 0);
    debug("control command handled");
}
