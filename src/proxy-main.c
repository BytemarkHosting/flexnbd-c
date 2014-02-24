#include <signal.h>

#include "mode.h"
#include "util.h"
#include "proxy.h"


static struct option proxy_options[] = {
	GETOPT_HELP,
	GETOPT_ADDR,
	GETOPT_PORT,
	GETOPT_CONNECT_ADDR,
	GETOPT_CONNECT_PORT,
	GETOPT_BIND,
	GETOPT_QUIET,
	GETOPT_VERBOSE,
	{0}
};
static char proxy_short_options[] = "hl:p:C:P:b:" SOPT_QUIET SOPT_VERBOSE;
static char proxy_help_text[] =
	"Usage: flexnbd-proxy <options>\n\n"
	"Resiliently proxy an NBD connection between client and server\n"
	"We can listen on TCP or UNIX socket, but only connect to TCP servers.\n\n"
	HELP_LINE
	"\t--" OPT_ADDR ",-l <ADDR>\tThe address we will bind to as a proxy.\n"
	"\t--" OPT_PORT ",-p <PORT>\tThe port we will bind to as a proxy, if required.\n"
	"\t--" OPT_CONNECT_ADDR ",-C <ADDR>\tAddress of the proxied server.\n"
	"\t--" OPT_CONNECT_PORT ",-P <PORT>\tPort of the proxied server.\n"
	"\t--" OPT_BIND ",-b <ADDR>\tThe address we connect from, as a proxy.\n"
	QUIET_LINE
	VERBOSE_LINE;

void read_proxy_param(
			int c,
			char **downstream_addr,
			char **downstream_port,
			char **upstream_addr,
			char **upstream_port,
			char **bind_addr )
{
	switch( c ) {
		case 'h' :
			fprintf( stdout, "%s\n", proxy_help_text );
			exit( 0 );
		case 'l':
			*downstream_addr = optarg;
			break;
		case 'p':
			*downstream_port = optarg;
			break;
		case 'C':
			*upstream_addr = optarg;
			break;
		case 'P':
			*upstream_port = optarg;
			break;
		case 'b':
			*bind_addr = optarg;
			break;
		case 'q':
			log_level = QUIET_LOG_LEVEL;
			break;
		case 'v':
			log_level = VERBOSE_LOG_LEVEL;
			break;
		default:
			exit_err( proxy_help_text );
			break;
	}
}

struct proxier * proxy = NULL;

void my_exit(int signum)
{
	info( "Exit signalled (%i)", signum );
	if ( NULL != proxy ) {
		proxy_cleanup( proxy );
	};
	exit( 0 );
}

int main( int argc, char *argv[] )
{
	int c;
	char *downstream_addr = NULL;
	char *downstream_port = NULL;
	char *upstream_addr   = NULL;
	char *upstream_port   = NULL;
	char *bind_addr       = NULL;
	int success;

	sigset_t mask;
	struct sigaction exit_action;

	sigemptyset( &mask );
	sigaddset( &mask, SIGTERM );
	sigaddset( &mask, SIGQUIT );
	sigaddset( &mask, SIGINT );

	exit_action.sa_handler = my_exit;
	exit_action.sa_mask = mask;
	exit_action.sa_flags = 0;

	while (1) {
		c = getopt_long( argc, argv, proxy_short_options, proxy_options, NULL );
		if ( -1 == c ) { break; }
		read_proxy_param( c,
				&downstream_addr,
				&downstream_port,
				&upstream_addr,
				&upstream_port,
				&bind_addr
		);
	}

	if ( NULL == downstream_addr  ){
		fprintf( stderr, "--addr is required.\n" );
		exit_err( proxy_help_text );
	} else if ( NULL == upstream_addr || NULL == upstream_port ){
		fprintf( stderr, "both --conn-addr and --conn-port are required.\n" );
		exit_err( proxy_help_text );
	}

	proxy = proxy_create(
		downstream_addr,
		downstream_port,
		upstream_addr,
		upstream_port,
		bind_addr
	);

	/* Set these *after* proxy has been assigned to */
	sigaction(SIGTERM, &exit_action, NULL);
	sigaction(SIGQUIT, &exit_action, NULL);
	sigaction(SIGINT,  &exit_action, NULL);
	signal(SIGPIPE, SIG_IGN); /* calls to splice() unhelpfully throw this */

	if ( NULL != downstream_port ) {
		info(
			"Proxying between %s %s (downstream) and %s %s (upstream)",
			downstream_addr, downstream_port, upstream_addr, upstream_port
		);
	} else {
		info(
			"Proxying between %s (downstream) and %s %s (upstream)",
			downstream_addr, upstream_addr, upstream_port
		);
	}

	success = do_proxy( proxy );
	proxy_destroy( proxy );

	return success ? 0 : 1;
}

