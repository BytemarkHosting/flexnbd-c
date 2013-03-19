#include <signal.h>
#include <sys/signalfd.h>

#include "mode.h"
#include "util.h"
#include "sockutil.h"
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
	"Resiliently proxy an NBD connection between client and server\n\n"
	HELP_LINE
	"\t--" OPT_ADDR ",-l <ADDR>\tThe address we will bind to as a proxy.\n"
	"\t--" OPT_PORT ",-p <PORT>\tThe port we will bind to as a proxy.\n"
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
			break;
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

/* Stolen from flexnbd.c, wil change in the near future so no point DRYing */
int build_signal_fd(void)
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

struct proxier* flexnbd_create_proxying(
	int signal_fd,
	char* s_downstream_address,
	char* s_downstream_port,
	char* s_upstream_address,
	char* s_upstream_port,
	char* s_upstream_bind
)
{
	struct proxier* proxy =  proxy_create(
		signal_fd,
		s_downstream_address,
		s_downstream_port,
		s_upstream_address,
		s_upstream_port,
		s_upstream_bind
	);

	return proxy;
}


int main( int argc, char *argv[] )
{
	int c;
	struct proxier * proxy;
	char *downstream_addr = NULL;
	char *downstream_port = NULL;
	char *upstream_addr   = NULL;
	char *upstream_port   = NULL;
	char *bind_addr       = NULL;
	int signal_fd;
	int success;

	signal(SIGPIPE, SIG_IGN); /* calls to splice() unhelpfully throw this */
	error_init();

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

	if ( NULL == downstream_addr || NULL == downstream_port ){
		fprintf( stderr, "both --addr and --port are required.\n" );
		exit_err( proxy_help_text );
	} else if ( NULL == upstream_addr || NULL == upstream_port ){
		fprintf( stderr, "both --conn-addr and --conn-port are required.\n" );
		exit_err( proxy_help_text );
	}

	signal_fd = build_signal_fd();

	proxy = flexnbd_create_proxying(
		signal_fd,
		downstream_addr,
		downstream_port,
		upstream_addr,
		upstream_port,
		bind_addr
	);

	info(
		"Proxying between %s %s (downstream) and %s %s (upstream)",
		downstream_addr, downstream_port, upstream_addr, upstream_port
	);

    success = do_proxy( proxy );
    sock_try_close( signal_fd );

	return success ? 0 : 1;
}

