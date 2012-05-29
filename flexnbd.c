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

#include "params.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

void syntax()
{
	fprintf(stderr, 
	"Syntax: flexnbd serve  <listen IP address> <port> <file> \\\n"
	"                          [full path to control socket] \\\n"
	"                          [allowed connection addresses ...]\n"
	"        flexnbd read   <IP address> <port> <offset> <length> > data\n"
	"        flexnbd write  <IP address> <port> <offset> <length> < data\n"
	"        flexnbd write  <IP address> <port> <offset> <file to write>\n"
	"        flexnbd acl    <control socket> [allowed connection addresses ...]\n"
	"        flexnbd mirror <control socket> <dst IP address> <dst port>\n"
	"                          [bytes per second] [proxy|nothing|exit]"
	"        flexnbd status <control socket>\n"
	);
	exit(1);
}

void params_serve(
	struct mode_serve_params* out, 
	char* s_ip_address, 
	char* s_port, 
	char* s_file,
	int acl_entries,
	char** s_acl_entries /* first may actually be path to control socket */
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
	
	out->control_socket_name = NULL;
	
	if (acl_entries > 0 && s_acl_entries[0][0] == '/') {
		out->control_socket_name = s_acl_entries[0];
		s_acl_entries++;
		acl_entries--;		
	}
	
	out->acl_entries = acl_entries;
	parsed = parse_acl(&out->acl, acl_entries, s_acl_entries);
	if (parsed != acl_entries)
		SERVER_ERROR("Bad ACL entry '%s'", s_acl_entries[parsed]);
	
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
	
	/* FIXME: duplicated from above */
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

void do_serve(struct mode_serve_params* params);
void do_read(struct mode_readwrite_params* params);
void do_write(struct mode_readwrite_params* params);
void do_remote_command(char* command, char* mode, int argc, char** argv);

union mode_params {
	struct mode_serve_params serve;
	struct mode_readwrite_params readwrite;
};

void mode(char* mode, int argc, char **argv)
{
	union mode_params params;
	memset(&params, 0, sizeof(params));
	
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
	else if (strcmp(mode, "acl") == 0 || strcmp(mode, "mirror") == 0 || strcmp(mode, "status") == 0) {
		if (argc >= 1) {
			do_remote_command(mode, argv[0], argc-1, argv+1);
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
	signal(SIGPIPE, SIG_IGN); /* calls to splice() unhelpfully throw this */
	error_init(); 
	
	if (argc < 2)
		syntax();
	mode(argv[1], argc-2, argv+2); /* never returns */
	
	return 0;
}

