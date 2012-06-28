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
#include "listen.h"
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

	sigemptyset( &mask );
	sigaddset( &mask, SIGTERM );
	sigaddset( &mask, SIGQUIT );
	sigaddset( &mask, SIGINT );

	FATAL_UNLESS( 0 == pthread_sigmask( SIG_BLOCK, &mask, NULL ), 
			"Signal blocking failed" );

	sfd = signalfd( -1, &mask, 0 );
	FATAL_IF( -1 == sfd, "Failed to get a signal fd" );

	return sfd;
}


void flexnbd_create_shared(
	struct flexnbd * flexnbd, 
	const char * s_ctrl_sock)
{
	NULLCHECK( flexnbd );
	if ( s_ctrl_sock ){
		flexnbd->control = 
			control_create( flexnbd, s_ctrl_sock );
	}
	else {
		flexnbd->control = NULL;
	}

	flexnbd->signal_fd = flexnbd_build_signal_fd();

	pthread_mutex_init( &flexnbd->switch_mutex, NULL );
}


struct flexnbd * flexnbd_create_serving(
	char* s_ip_address,
	char* s_port,
	char* s_file,
	char *s_ctrl_sock,
	int default_deny,
	int acl_entries,
	char** s_acl_entries,
	int max_nbd_clients)
{
	struct flexnbd * flexnbd = xmalloc( sizeof( struct flexnbd ) );
	flexnbd->serve = server_create( 
			flexnbd,
			s_ip_address,
			s_port,
			s_file, 
			default_deny,
			acl_entries,
			s_acl_entries,
			max_nbd_clients, 
			1);
	flexnbd_create_shared( flexnbd, s_ctrl_sock );
	return flexnbd;
}


struct flexnbd * flexnbd_create_listening(
		char* s_ip_address, 
		char* s_rebind_ip_address, 
		char* s_port, 
		char* s_rebind_port, 
		char* s_file,
		char *s_ctrl_sock, 
		int default_deny,
		int acl_entries, 
		char** s_acl_entries,
		int max_nbd_clients )
{
	struct flexnbd * flexnbd = xmalloc( sizeof( struct flexnbd ) );
	flexnbd->listen = listen_create(
			flexnbd,
			s_ip_address,
			s_rebind_ip_address,
			s_port,
			s_rebind_port,
			s_file,
			default_deny,
			acl_entries,
			s_acl_entries,
			max_nbd_clients);
	flexnbd->serve = flexnbd->listen->init_serve;
	flexnbd_create_shared( flexnbd, s_ctrl_sock );
	return flexnbd;
}


void flexnbd_spawn_control(struct flexnbd * flexnbd )
{
	NULLCHECK( flexnbd );
	NULLCHECK( flexnbd->control );

	pthread_t * control_thread = &flexnbd->control->thread;

	FATAL_UNLESS( 0 == pthread_create( 
				control_thread, 
				NULL, 
				control_runner, 
				flexnbd->control ),
			"Couldn't create the control thread" );
}

void flexnbd_stop_control( struct flexnbd * flexnbd )
{
	NULLCHECK( flexnbd );
	NULLCHECK( flexnbd->control );

	control_signal_close( flexnbd->control );
	FATAL_UNLESS( 0 == pthread_join( flexnbd->control->thread, NULL ),
			"Failed joining the control thread" );
}


int flexnbd_signal_fd( struct flexnbd * flexnbd )
{
	NULLCHECK( flexnbd );
	return flexnbd->signal_fd;
}

void flexnbd_destroy( struct flexnbd * flexnbd )
{
	NULLCHECK( flexnbd );
	if ( flexnbd->control ) {
		control_destroy( flexnbd->control );
	}
	if ( flexnbd->listen ) {
		listen_destroy( flexnbd->listen );
	}

	close( flexnbd->signal_fd );
	free( flexnbd );
}


/* THOU SHALT NOT DEREFERENCE flexnbd->serve OUTSIDE A SWITCH LOCK
 */
void flexnbd_switch_lock( struct flexnbd * flexnbd )
{
	NULLCHECK( flexnbd );
	pthread_mutex_lock( &flexnbd->switch_mutex );
}

void flexnbd_switch_unlock( struct flexnbd * flexnbd )
{
	NULLCHECK( flexnbd );
	pthread_mutex_unlock( &flexnbd->switch_mutex );
}

struct server * flexnbd_server( struct flexnbd * flexnbd )
{
	NULLCHECK( flexnbd );
	return flexnbd->serve;
}


void flexnbd_replace_acl( struct flexnbd * flexnbd, struct acl * acl )
{
	NULLCHECK( flexnbd );
	flexnbd_switch_lock( flexnbd );
	{
		server_replace_acl( flexnbd_server(flexnbd), acl );
	}
	flexnbd_switch_unlock( flexnbd );
}


struct status * flexnbd_status_create( struct flexnbd * flexnbd )
{
	NULLCHECK( flexnbd );
	struct status * status;
	
	flexnbd_switch_lock( flexnbd );
	{
		status = status_create( flexnbd_server( flexnbd ) );
	}
	flexnbd_switch_unlock( flexnbd );
	return status;
}

/** THOU SHALT *ONLY* CALL THIS FROM INSIDE A SWITCH LOCK
 */
void flexnbd_set_server( struct flexnbd * flexnbd, struct server * serve )
{
	NULLCHECK( flexnbd );
	flexnbd->serve = serve;
}


/* Calls the given callback to exchange server objects, then sets
 * flexnbd->server so everything else can see it. */
void flexnbd_switch( struct flexnbd * flexnbd, struct server *(listen_cb)(struct listen *) )
{
	NULLCHECK( flexnbd );
	NULLCHECK( flexnbd->listen );

	flexnbd_switch_lock( flexnbd );
	{
		struct server * new_server = listen_cb( flexnbd->listen );
		NULLCHECK( new_server );
		flexnbd_set_server( flexnbd, new_server );
	}
	flexnbd_switch_unlock( flexnbd );

}

/* Get the default_deny of the current server object.  This takes the
 * switch_lock to avoid nastiness if the server switches and gets freed
 * in the dereference chain.
 * This means that this function must not be called if the switch lock
 * is already held.
 */
int flexnbd_default_deny( struct flexnbd * flexnbd )
{
	int result;

	NULLCHECK( flexnbd );
	flexnbd_switch_lock( flexnbd );
	{
		result = server_default_deny( flexnbd->serve );
	}
	flexnbd_switch_unlock( flexnbd );
	return result;
}


int flexnbd_serve( struct flexnbd * flexnbd )
{
	NULLCHECK( flexnbd );
	int success;

	if ( flexnbd->control ){
		debug( "Spawning control thread" );
		flexnbd_spawn_control( flexnbd );
	}

	if ( flexnbd->listen ){
		success = do_listen( flexnbd->listen );
	}
	else {
		do_serve( flexnbd->serve );
		/* We can't tell here what the intent was.  We can
		 * legitimately exit either in control or not.
		 */
		success = 1;
	}

	if ( flexnbd->control ) {
		debug( "Stopping control thread" );
		flexnbd_stop_control( flexnbd );
		debug("Control thread stopped");
	}

	return success;
}

