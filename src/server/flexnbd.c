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

/** main() function for parsing and dispatching commands.  Each mode has
 *  a corresponding structure which is filled in and passed to a do_ function
 *  elsewhere in the program.
 */

#include "flexnbd.h"
#include "serve.h"
#include "util.h"
#include "control.h"
#include "status.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/signalfd.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <getopt.h>

#include "acl.h"


int flexnbd_build_signal_fd(void)
{
    sigset_t mask;
    int sfd;

    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGINT);

    FATAL_UNLESS(0 == pthread_sigmask(SIG_BLOCK, &mask, NULL),
		 "Signal blocking failed");

    sfd = signalfd(-1, &mask, 0);
    FATAL_IF(-1 == sfd, "Failed to get a signal fd");

    return sfd;
}


void flexnbd_create_shared(struct flexnbd *flexnbd,
			   const char *s_ctrl_sock)
{
    NULLCHECK(flexnbd);
    if (s_ctrl_sock) {
	flexnbd->control = control_create(flexnbd, s_ctrl_sock);
    } else {
	flexnbd->control = NULL;
    }

    flexnbd->signal_fd = flexnbd_build_signal_fd();
}


struct flexnbd *flexnbd_create_serving(char *s_ip_address,
				       char *s_port,
				       char *s_file,
				       char *s_ctrl_sock,
				       int default_deny,
				       int acl_entries,
				       char **s_acl_entries,
				       int max_nbd_clients,
				       int use_killswitch)
{
    struct flexnbd *flexnbd = xmalloc(sizeof(struct flexnbd));
    flexnbd->serve = server_create(flexnbd,
				   s_ip_address,
				   s_port,
				   s_file,
				   default_deny,
				   acl_entries,
				   s_acl_entries,
				   max_nbd_clients, use_killswitch, 1);
    flexnbd_create_shared(flexnbd, s_ctrl_sock);

    // Beats installing one handler per client instance
    if (use_killswitch) {
	struct sigaction act = {
	    .sa_sigaction = client_killswitch_hit,
	    .sa_flags = SA_RESTART | SA_SIGINFO
	};

	FATAL_UNLESS(0 == sigaction(CLIENT_KILLSWITCH_SIGNAL, &act, NULL),
		     "Installing client killswitch signal failed");
    }

    return flexnbd;
}

struct flexnbd *flexnbd_create_listening(char *s_ip_address,
					 char *s_port,
					 char *s_file,
					 char *s_ctrl_sock,
					 int default_deny,
					 int acl_entries,
					 char **s_acl_entries)
{
    struct flexnbd *flexnbd = xmalloc(sizeof(struct flexnbd));
    flexnbd->serve = server_create(flexnbd,
				   s_ip_address,
				   s_port,
				   s_file,
				   default_deny,
				   acl_entries, s_acl_entries, 1, 0, 0);
    flexnbd_create_shared(flexnbd, s_ctrl_sock);

    // listen can't use killswitch, as mirror may pause on sending things
    // for a very long time.

    return flexnbd;
}

void flexnbd_spawn_control(struct flexnbd *flexnbd)
{
    NULLCHECK(flexnbd);
    NULLCHECK(flexnbd->control);

    pthread_t *control_thread = &flexnbd->control->thread;

    FATAL_UNLESS(0 == pthread_create(control_thread,
				     NULL,
				     control_runner,
				     flexnbd->control),
		 "Couldn't create the control thread");
}

void flexnbd_stop_control(struct flexnbd *flexnbd)
{
    NULLCHECK(flexnbd);
    NULLCHECK(flexnbd->control);

    control_signal_close(flexnbd->control);
    pthread_t tid = flexnbd->control->thread;
    FATAL_UNLESS(0 == pthread_join(tid, NULL),
		 "Failed joining the control thread");
    debug("Control thread %p pthread_join returned", tid);
}


int flexnbd_signal_fd(struct flexnbd *flexnbd)
{
    NULLCHECK(flexnbd);
    return flexnbd->signal_fd;
}

void flexnbd_destroy(struct flexnbd *flexnbd)
{
    NULLCHECK(flexnbd);
    if (flexnbd->control) {
	control_destroy(flexnbd->control);
    }

    close(flexnbd->signal_fd);
    free(flexnbd);
}


struct server *flexnbd_server(struct flexnbd *flexnbd)
{
    NULLCHECK(flexnbd);
    return flexnbd->serve;
}

void flexnbd_replace_acl(struct flexnbd *flexnbd, struct acl *acl)
{
    NULLCHECK(flexnbd);
    server_replace_acl(flexnbd_server(flexnbd), acl);
}


struct status *flexnbd_status_create(struct flexnbd *flexnbd)
{
    NULLCHECK(flexnbd);
    struct status *status;

    status = status_create(flexnbd_server(flexnbd));
    return status;
}

void flexnbd_set_server(struct flexnbd *flexnbd, struct server *serve)
{
    NULLCHECK(flexnbd);
    flexnbd->serve = serve;
}


/* Get the default_deny of the current server object. */
int flexnbd_default_deny(struct flexnbd *flexnbd)
{
    NULLCHECK(flexnbd);
    return server_default_deny(flexnbd->serve);
}


void make_writable(const char *filename)
{
    NULLCHECK(filename);
    FATAL_IF_NEGATIVE(chmod(filename, S_IWUSR),
		      "Couldn't chmod %s: %s", filename, strerror(errno));
}


int flexnbd_serve(struct flexnbd *flexnbd)
{
    NULLCHECK(flexnbd);
    int success;
    struct self_pipe *open_signal = NULL;

    if (flexnbd->control) {
	debug("Spawning control thread");
	flexnbd_spawn_control(flexnbd);
	open_signal = flexnbd->control->open_signal;
    }

    success = do_serve(flexnbd->serve, open_signal);
    debug("do_serve success is %d", success);

    if (flexnbd->control) {
	debug("Stopping control thread");
	flexnbd_stop_control(flexnbd);
	debug("Control thread stopped");
    }

    return success;
}
