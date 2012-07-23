#include "mode.h"
#include "flexnbd.h"

#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


static struct option serve_options[] = {
	GETOPT_HELP,
	GETOPT_ADDR,
	GETOPT_PORT,
	GETOPT_FILE,
	GETOPT_SOCK,
	GETOPT_DENY,
	GETOPT_QUIET,
	GETOPT_VERBOSE,
	{0}
};
static char serve_short_options[] = "hl:p:f:s:d" SOPT_QUIET SOPT_VERBOSE;
static char serve_help_text[] =
	"Usage: flexnbd " CMD_SERVE " <options> [<acl address>*]\n\n"
	"Serve FILE from ADDR:PORT, with an optional control socket at SOCK.\n\n"
	HELP_LINE
	"\t--" OPT_ADDR ",-l <ADDR>\tThe address to serve on.\n"
	"\t--" OPT_PORT ",-p <PORT>\tThe port to serve on.\n"
	"\t--" OPT_FILE ",-f <FILE>\tThe file to serve.\n"
	"\t--" OPT_DENY ",-d\tDeny connections by default unless in ACL.\n"
	SOCK_LINE
	VERBOSE_LINE
	QUIET_LINE;


static struct option * listen_options = serve_options;
static char  * listen_short_options = serve_short_options;
static char listen_help_text[] =
	"Usage: flexnbd " CMD_LISTEN " <options> [<acl_address>*]\n\n"
	"Listen for an incoming migration on ADDR:PORT.\n\n"
	HELP_LINE
	"\t--" OPT_ADDR ",-l <ADDR>\tThe address to listen on.\n"
	"\t--" OPT_PORT ",-p <PORT>\tThe port to listen on.\n"
	"\t--" OPT_FILE ",-f <FILE>\tThe file to write to.\n"
	"\t--" OPT_DENY ",-d\tDeny connections by default unless in ACL.\n"
	SOCK_LINE
	VERBOSE_LINE
	QUIET_LINE;


static struct option read_options[] = {
	GETOPT_HELP,
	GETOPT_ADDR,
	GETOPT_PORT,
	GETOPT_FROM,
	GETOPT_SIZE,
	GETOPT_BIND,
	GETOPT_QUIET,
	GETOPT_VERBOSE,
	{0}
};
static char read_short_options[] = "hl:p:F:S:b:" SOPT_QUIET SOPT_VERBOSE;
static char read_help_text[] =
	"Usage: flexnbd " CMD_READ " <options>\n\n"
	"Read SIZE bytes from a server at ADDR:PORT to stdout, starting at OFFSET.\n\n"
	HELP_LINE
	"\t--" OPT_ADDR ",-l <ADDR>\tThe address to read from.\n"
	"\t--" OPT_PORT ",-p <PORT>\tThe port to read from.\n"
	"\t--" OPT_FROM ",-F <OFFSET>\tByte offset to read from.\n"
	"\t--" OPT_SIZE ",-S <SIZE>\tBytes to read.\n"
	BIND_LINE
	VERBOSE_LINE
	QUIET_LINE;


static struct option *write_options = read_options;
static char *write_short_options = read_short_options;
static char write_help_text[] =
	"Usage: flexnbd " CMD_WRITE" <options>\n\n"
	"Write SIZE bytes from stdin to a server at ADDR:PORT, starting at OFFSET.\n\n"
	HELP_LINE
	"\t--" OPT_ADDR ",-l <ADDR>\tThe address to write to.\n"
	"\t--" OPT_PORT ",-p <PORT>\tThe port to write to.\n"
	"\t--" OPT_FROM ",-F <OFFSET>\tByte offset to write from.\n"
	"\t--" OPT_SIZE ",-S <SIZE>\tBytes to write.\n"
	BIND_LINE
	VERBOSE_LINE
	QUIET_LINE;

static struct option acl_options[] = {
	GETOPT_HELP,
	GETOPT_SOCK,
	GETOPT_QUIET,
	GETOPT_VERBOSE,
	{0}
};
static char acl_short_options[] = "hs:" SOPT_QUIET SOPT_VERBOSE;
static char acl_help_text[] =
	"Usage: flexnbd " CMD_ACL " <options> [<acl address>+]\n\n"
	"Set the access control list for a server with control socket SOCK.\n\n"
	HELP_LINE
	SOCK_LINE
	VERBOSE_LINE
	QUIET_LINE;

static struct option mirror_options[] = {
	GETOPT_HELP,
	GETOPT_SOCK,
	GETOPT_ADDR,
	GETOPT_PORT,
	GETOPT_BIND,
	GETOPT_QUIET,
	GETOPT_VERBOSE,
	{0}
};
static char mirror_short_options[] = "hs:l:p:b:" SOPT_QUIET SOPT_VERBOSE;
static char mirror_help_text[] =
	"Usage: flexnbd " CMD_MIRROR " <options>\n\n"
	"Start mirroring from the server with control socket SOCK to one at ADDR:PORT.\n\n"
	HELP_LINE
	"\t--" OPT_ADDR ",-l <ADDR>\tThe address to mirror to.\n"
	"\t--" OPT_PORT ",-p <PORT>\tThe port to mirror to.\n"
	SOCK_LINE
	BIND_LINE
	VERBOSE_LINE
	QUIET_LINE;

static struct option break_options[] = {
	GETOPT_HELP,
	GETOPT_SOCK,
	GETOPT_QUIET,
	GETOPT_VERBOSE,
	{0}
};
static char break_short_options[] = "hs:" SOPT_QUIET SOPT_VERBOSE;
static char break_help_text[] = 
	"Usage: flexnbd " CMD_BREAK " <options>\n\n"
	"Stop mirroring from the server with control socket SOCK.\n\n"
	HELP_LINE
	SOCK_LINE
	VERBOSE_LINE
	QUIET_LINE;


static struct option status_options[] = {
	GETOPT_HELP,
	GETOPT_SOCK,
	GETOPT_QUIET,
	GETOPT_VERBOSE,
	{0}
};
static char status_short_options[] = "hs:" SOPT_QUIET SOPT_VERBOSE;
static char status_help_text[] =
	"Usage: flexnbd " CMD_STATUS " <options>\n\n"
	"Get the status for a server with control socket SOCK.\n\n"
	HELP_LINE
	SOCK_LINE
	VERBOSE_LINE
	QUIET_LINE;

char help_help_text_arr[] =
	"Usage: flexnbd <cmd> [cmd options]\n\n"
	"Commands:\n"
	"\tflexnbd serve\n"
	"\tflexnbd read\n"
	"\tflexnbd write\n"
	"\tflexnbd acl\n"
	"\tflexnbd mirror\n"
	"\tflexnbd status\n"
	"\tflexnbd help\n\n"
	"See flexnbd help <cmd> for further info\n";
/* Slightly odd array/pointer pair to stop the compiler from complaining
 * about symbol sizes
 */
char * help_help_text = help_help_text_arr;



int do_serve(struct server* params);
void do_read(struct mode_readwrite_params* params);
void do_write(struct mode_readwrite_params* params);
void do_remote_command(char* command, char* mode, int argc, char** argv);


void read_serve_param( int c, char **ip_addr, char **ip_port, char **file, char **sock, int *default_deny )
{
	switch(c){
		case 'h':
			fprintf(stdout, "%s\n", serve_help_text );
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
		case 'q':
			log_level = 4;
			break;
		case 'v':
			log_level = VERBOSE_LOG_LEVEL;
			break;
		default:
			exit_err( serve_help_text );
			break;
	}
}


void read_listen_param( int c, 
		char **ip_addr, 
		char **ip_port, 
		char **file, 
		char **sock, 
		int *default_deny )
{
	switch(c){
		case 'h':
			fprintf(stdout, "%s\n", listen_help_text );
			exit(0);
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
		case 'q':
			log_level = 4;
			break;
		case 'v':
			log_level = VERBOSE_LOG_LEVEL;
			break;
		default:
			exit_err( listen_help_text );
			break;
	}
}

void read_readwrite_param( int c, char **ip_addr, char **ip_port, char **bind_addr, char **from, char **size)
{
	switch(c){
		case 'h':
			fprintf(stdout, "%s\n", read_help_text );
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
		case 'q':
			log_level = 4;
			break;
		case 'v':
			log_level = VERBOSE_LOG_LEVEL;
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
			fprintf( stdout, "%s\n", help_text );
			exit( 0 );
			break;
		case 's':
			*sock = optarg;
			break;
		case 'q':
			log_level = 4;
			break;
		case 'v':
			log_level = VERBOSE_LOG_LEVEL;
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
			fprintf( stdout, "%s\n", mirror_help_text );
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
			break;
		case 'q':
			log_level = 4;
			break;
		case 'v':
			log_level = VERBOSE_LOG_LEVEL;
			break;
		default:
			exit_err( mirror_help_text );
			break;
	}
}

void read_break_param( int c, char **sock )
{
	switch( c ) {
		case 'h':
			fprintf( stdout, "%s\n", break_help_text );
			exit( 0 );
			break;
		case 's':
			*sock = optarg;
			break;
		case 'q':
			log_level = 4;
			break;
		case 'v':
			log_level = VERBOSE_LOG_LEVEL;
			break;
		default:
			exit_err( break_help_text );
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

	struct flexnbd * flexnbd;

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

	flexnbd = flexnbd_create_serving( 
		ip_addr, 
		ip_port, 
		file, 
		sock, 
		default_deny, 
		argc - optind, 
		argv + optind,
		MAX_NBD_CLIENTS );
	flexnbd_serve( flexnbd );
	flexnbd_destroy( flexnbd );

	return 0;
}


int mode_listen( int argc, char *argv[] )
{
	int c;
	char *ip_addr = NULL;
	char *ip_port = NULL;
	char *file    = NULL;
	char *sock    = NULL;
	int default_deny = 0; // not on by default
	int err = 0;

	int success;

	struct flexnbd * flexnbd;

	while (1) {
		c = getopt_long(argc, argv, listen_short_options, listen_options, NULL);
		if ( c == -1 ) { break; }

		read_listen_param( c, &ip_addr, &ip_port, 
				&file, &sock, &default_deny );
	}

	if ( NULL == ip_addr || NULL == ip_port ) {
		err = 1;
		fprintf( stderr, "both --addr and --port are required.\n" );
	}
	if ( NULL == file ) {
		err = 1;
		fprintf( stderr, "--file is required\n" );
	}
	if ( err ) { exit_err( listen_help_text ); }

	flexnbd = flexnbd_create_listening( 
		ip_addr, 
		ip_port, 
		file,
		sock,
		default_deny, 
		argc - optind, 
		argv + optind );
	success = flexnbd_serve( flexnbd );
	flexnbd_destroy( flexnbd );

	return success ? 0 : 1;
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


int mode_break( int argc, char *argv[] )
{
	int c;
	char *sock = NULL;

	while (1) {
		c = getopt_long( argc, argv, break_short_options, break_options, NULL );
		if ( -1 == c ) { break; }
		read_break_param( c, &sock );
	}

	if ( NULL == sock ){
		fprintf( stderr, "--sock is required.\n" );
		exit_err( acl_help_text );
	}

	do_remote_command( "break", sock, argc - optind, argv + optind );

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
	char *help_text = NULL;

	if ( argc < 1 ){
		help_text = help_help_text;
	} else {
		cmd = argv[0];
		if (IS_CMD( CMD_SERVE, cmd ) ) {
			help_text = serve_help_text;
		} else if ( IS_CMD( CMD_LISTEN, cmd ) ) {
			help_text = listen_help_text;
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

	fprintf( stdout, "%s\n", help_text );
	return 0;
}


void mode(char* mode, int argc, char **argv)
{
	if ( IS_CMD( CMD_SERVE, mode ) ) {
		exit( mode_serve( argc, argv ) );
	}
	else if ( IS_CMD( CMD_LISTEN, mode ) ) {
		exit( mode_listen( argc, argv ) );
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
       	else if ( IS_CMD( CMD_BREAK, mode ) ) {
		mode_break( argc, argv );
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


