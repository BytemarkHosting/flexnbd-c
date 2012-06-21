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


#include "serve.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <getopt.h>

#include "options.h"
#include "acl.h"


void exit_err( char *msg )
{
	fprintf( stderr, msg );
	exit( 1 );
}


/* TODO: Separate this function.
 * It should be:
 * params_read( struct mode_readwrite_params* out,
 * 		char *s_ip_address,
 * 		char *s_port,
 * 		char *s_from,
 * 		char *s_length )
 * params_write( struct mode_readwrite_params* out,
 * 		 char *s_ip_address,
 *		 char *s_port,
 *		 char *s_from,
 *		 char *s_length,
 *		 char *s_filename )
 */
void params_readwrite(
	int write_not_read,
	struct mode_readwrite_params* out,
	char* s_ip_address,
	char* s_port,
	char* s_bind_address,
	char* s_from,
	char* s_length_or_filename
)
{
	FATAL_IF_NULL(s_ip_address, "No IP address supplied");
	FATAL_IF_NULL(s_port, "No port number supplied");
	FATAL_IF_NULL(s_from, "No from supplied");
	FATAL_IF_NULL(s_length_or_filename, "No length supplied");

	FATAL_IF_ZERO(
		parse_ip_to_sockaddr(&out->connect_to.generic, s_ip_address),
		"Couldn't parse connection address '%s'",
		s_ip_address
	);

	if (s_bind_address != NULL && 
			parse_ip_to_sockaddr(&out->connect_from.generic, s_bind_address) == 0) {
		fatal("Couldn't parse bind address '%s'", s_bind_address);
	}

	parse_port( s_port, &out->connect_to.v4 );

	out->from  = atol(s_from);

	if (write_not_read) {
		if (s_length_or_filename[0]-48 < 10) {
			out->len   = atol(s_length_or_filename);
			out->data_fd = 0;
		}
		else {
			out->data_fd = open(
			  s_length_or_filename, O_RDONLY);
			FATAL_IF_NEGATIVE(out->data_fd,
			  "Couldn't open %s", s_length_or_filename);
			out->len = lseek64(out->data_fd, 0, SEEK_END);
			FATAL_IF_NEGATIVE(out->len,
			  "Couldn't find length of %s", s_length_or_filename);
			FATAL_IF_NEGATIVE(
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

void do_serve(struct server* params);
void do_read(struct mode_readwrite_params* params);
void do_write(struct mode_readwrite_params* params);
void do_remote_command(char* command, char* mode, int argc, char** argv);

void read_serve_param( int c, char **ip_addr, char **ip_port, char **file, char **sock, int *default_deny )

{
	switch(c){
		case 'h':
			fprintf(stdout, serve_help_text );
			exit( 0 );
			break;
		case 'l':
			*ip_addr = optarg;
			break;
		case 'p':
			*ip_port = optarg;
			break;
		case 'f':
			*file = optarg;
			break;
		case 's':
			*sock = optarg;
			break;
		case 'd':
			*default_deny = 1;
			break;
		case 'v':
			log_level = 0;
			break;
		default:
			exit_err( serve_help_text );
			break;
	}
}


void read_readwrite_param( int c, char **ip_addr, char **ip_port, char **bind_addr, char **from, char **size)
{
	switch(c){
		case 'h':
			fprintf(stdout, read_help_text );
			exit( 0 );
			break;
		case 'l':
			*ip_addr = optarg;
			break;
		case 'p':
			*ip_port = optarg;
			break;
		case 'F':
			*from = optarg;
			break;
		case 'S':
			*size = optarg;
			break;
		case 'b':
			*bind_addr = optarg;
			break;
		case 'v':
			log_level = 0;
			break;
		default:
			exit_err( read_help_text );
			break;
	}
}

void read_sock_param( int c, char **sock, char *help_text )
{
	switch(c){
		case 'h':
			fprintf( stdout, help_text );
			exit( 0 );
			break;
		case 's':
			*sock = optarg;
			break;
		case 'v':
			log_level = 0;
			break;
		default:
			exit_err( help_text );
			break;
	}
}

void read_acl_param( int c, char **sock )
{
	read_sock_param( c, sock, acl_help_text );
}

void read_mirror_param( int c, char **sock, char **ip_addr, char **ip_port, char **bind_addr )
{
	switch( c ){
		case 'h':
			fprintf( stdout, mirror_help_text );
			exit( 0 );
			break;
		case 's':
			*sock = optarg;
			break;
		case 'l':
			*ip_addr = optarg;
			break;
		case 'p':
			*ip_port = optarg;
			break;
		case 'b':
			*bind_addr = optarg;
		case 'v':
			log_level = 0;
			break;
		default:
			exit_err( mirror_help_text );
			break;
	}
}

void read_status_param( int c, char **sock )
{
	read_sock_param( c, sock, status_help_text );
}

int mode_serve( int argc, char *argv[] )
{
	int c;
	char *ip_addr = NULL;
	char *ip_port = NULL;
	char *file    = NULL;
	char *sock    = NULL;
	int default_deny = 0; // not on by default
	int err = 0;

	struct server * serve;

	while (1) {
		c = getopt_long(argc, argv, serve_short_options, serve_options, NULL);
		if ( c == -1 ) { break; }

		read_serve_param( c, &ip_addr, &ip_port, &file, &sock, &default_deny );
	}

	if ( NULL == ip_addr || NULL == ip_port ) {
		err = 1;
		fprintf( stderr, "both --addr and --port are required.\n" );
	}
	if ( NULL == file ) {
		err = 1;
		fprintf( stderr, "--file is required\n" );
	}
	if ( err ) { exit_err( serve_help_text ); }

	serve = server_create( ip_addr, ip_port, file, sock, default_deny, argc - optind, argv + optind, MAX_NBD_CLIENTS );
	do_serve( serve );
	server_destroy( serve );

	return 0;
}

int mode_read( int argc, char *argv[] )
{
	int c;
	char *ip_addr   = NULL;
	char *ip_port   = NULL;
	char *bind_addr = NULL;
	char *from = NULL;
	char *size = NULL;
	int err = 0;

	struct mode_readwrite_params readwrite;

	while (1){
		c = getopt_long(argc, argv, read_short_options, read_options, NULL);

		if ( c == -1 ) { break; }

		read_readwrite_param( c, &ip_addr, &ip_port, &bind_addr, &from, &size );
	}

	if ( NULL == ip_addr || NULL == ip_port ) {
		err = 1;
		fprintf( stderr, "both --addr and --port are required.\n" );
	}
	if ( NULL == from || NULL == size ) {
		err = 1;
		fprintf( stderr, "both --from and --size are required.\n" );
	}
	if ( err ) { exit_err( read_help_text ); }

	memset( &readwrite, 0, sizeof( readwrite ) );
	params_readwrite( 0, &readwrite, ip_addr, ip_port, bind_addr, from, size );
	do_read( &readwrite );
	return 0;
}

int mode_write( int argc, char *argv[] )
{
	int c;
	char *ip_addr   = NULL;
	char *ip_port   = NULL;
	char *bind_addr = NULL;
	char *from = NULL;
	char *size = NULL;
	int err = 0;

	struct mode_readwrite_params readwrite;

	while (1){
		c = getopt_long(argc, argv, write_short_options, write_options, NULL);
		if ( c == -1 ) { break; }

		read_readwrite_param( c, &ip_addr, &ip_port, &bind_addr, &from, &size );
	}

	if ( NULL == ip_addr || NULL == ip_port ) {
		err = 1;
		fprintf( stderr, "both --addr and --port are required.\n" );
	}
	if ( NULL == from || NULL == size ) {
		err = 1;
		fprintf( stderr, "both --from and --size are required.\n" );
	}
	if ( err ) { exit_err( write_help_text ); }

	memset( &readwrite, 0, sizeof( readwrite ) );
	params_readwrite( 1, &readwrite, ip_addr, ip_port, bind_addr, from, size );
	do_write( &readwrite );
	return 0;
}

int mode_acl( int argc, char *argv[] )
{
	int c;
	char *sock = NULL;

	while (1) {
		c = getopt_long( argc, argv, acl_short_options, acl_options, NULL );
		if ( c == -1 ) { break; }
		read_acl_param( c, &sock );
	}

	if ( NULL == sock ){
		fprintf( stderr, "--sock is required.\n" );
		exit_err( acl_help_text );
	}

	/* Don't use the CMD_ACL macro here, "acl" is the remote command
	 * name, not the cli option
	 */
	do_remote_command( "acl", sock, argc - optind, argv + optind );

	return 0;
}


int mode_mirror( int argc, char *argv[] )
{
	int c;
	char *sock = NULL;
	char *remote_argv[4] = {0};
	int err = 0;

	while (1) {
		c = getopt_long( argc, argv, mirror_short_options, mirror_options, NULL);
		if ( -1 == c ) { break; }
		read_mirror_param( c, &sock, &remote_argv[0], &remote_argv[1], &remote_argv[2] );
	}

	if ( NULL == sock ){
		fprintf( stderr, "--sock is required.\n" );
		err = 1;
	}
	if ( NULL == remote_argv[0] || NULL == remote_argv[1] ) {
		fprintf( stderr, "both --addr and --port are required.\n");
		err = 1;
	}
	if ( err ) { exit_err( mirror_help_text ); }
	
	if (remote_argv[2] == NULL) {
		do_remote_command( "mirror", sock, 2, remote_argv );
	}
	else {
		do_remote_command( "mirror", sock, 3, remote_argv );
	}

	return 0;
}


int mode_status( int argc, char *argv[] )
{
	int c;
	char *sock = NULL;

	while (1) {
		c = getopt_long( argc, argv, status_short_options, status_options, NULL );
		if ( -1 == c ) { break; }
		read_status_param( c, &sock );
	}

	if ( NULL == sock ){
		fprintf( stderr, "--sock is required.\n" );
		exit_err( acl_help_text );
	}

	do_remote_command( "status", sock, argc - optind, argv + optind );

	return 0;
}


int mode_help( int argc, char *argv[] )
{
	char *cmd;
	char *help_text;

	if ( argc < 1 ){
		help_text = help_help_text;
	} else {
		cmd = argv[0];
		if (IS_CMD( CMD_SERVE, cmd ) ) {
			help_text = serve_help_text;
		} else if ( IS_CMD( CMD_READ, cmd ) ) {
			help_text = read_help_text;
		} else if ( IS_CMD( CMD_WRITE, cmd ) ) {
			help_text = write_help_text;
		} else if ( IS_CMD( CMD_ACL, cmd ) ) {
			help_text = acl_help_text;
		} else if ( IS_CMD( CMD_MIRROR, cmd ) ) {
			help_text = mirror_help_text;
		} else if ( IS_CMD( CMD_STATUS, cmd ) ) {
			help_text = status_help_text;
		} else { exit_err( help_help_text ); }
	}

	fprintf( stdout, help_text );
	return 0;
}


void mode(char* mode, int argc, char **argv)
{
	if ( IS_CMD( CMD_SERVE, mode ) ) {
		mode_serve( argc, argv );
	}
	else if ( IS_CMD( CMD_READ, mode ) ) {
		mode_read( argc, argv );
	}
	else if ( IS_CMD( CMD_WRITE, mode ) ) {
		mode_write( argc, argv );
	}
	else if ( IS_CMD( CMD_ACL, mode ) ) {
		mode_acl( argc, argv );
	}
	else if ( IS_CMD( CMD_MIRROR, mode ) ) {
		mode_mirror( argc, argv );
	}
       	else if ( IS_CMD( CMD_STATUS, mode ) ) {
		mode_status( argc, argv );
	}
	else if ( IS_CMD( CMD_HELP, mode ) ) {
		mode_help( argc-1, argv+1 );
	}
	else {
		mode_help( argc-1, argv+1 );
		exit( 1 );
	}
	exit(0);
}


int main(int argc, char** argv)
{
	signal(SIGPIPE, SIG_IGN); /* calls to splice() unhelpfully throw this */
	error_init();

	if (argc < 2) {
		exit_err( help_help_text );
	}
	mode(argv[1], argc-1, argv+1); /* never returns */

	return 0;
}

