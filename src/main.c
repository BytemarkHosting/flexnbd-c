#include "util.h"
#include "mode.h"

#include <signal.h>

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

