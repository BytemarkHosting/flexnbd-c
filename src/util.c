#include <stdarg.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <unistd.h>

#include "util.h"

pthread_key_t cleanup_handler_key; 

int log_level = 2;

void error_init(void)
{
	pthread_key_create(&cleanup_handler_key, free);
}

void error_handler(int fatal __attribute__ ((unused)) )
{
	DECLARE_ERROR_CONTEXT(context);
	
	if (!context) {
		/* FIXME: This can't be right - by default we exit()
		 * with a status of 0 in this case.
		 */
		pthread_exit((void*) 1);
	}
		
	longjmp(context->jmp, fatal ? 1 : 2 );
}


void exit_err( const char *msg )
{
	fprintf( stderr, "%s\n", msg );
	exit( 1 );
}



void mylog(int line_level, const char* format, ...)
{
	va_list argptr;
	char * format_n = NULL;
	int format_len;

	
	if (line_level < log_level) { return; }
	
	/* Copy the format sideways so that we can append a "\n" to it
	 * and avoid a second fprintf. This stops log lines from getting
	 * interleaved.
	 */
	format_len = strlen( format );
	format_n = xmalloc( format_len + 2 );
	memcpy( format_n, format, format_len+1 );
	format_n[format_len] = '\n';

	va_start(argptr, format);
	vfprintf(stderr, format_n, argptr);
	va_end(argptr);

	free( format_n );
}

void* xrealloc(void* ptr, size_t size)
{
	void* p = realloc(ptr, size);
	FATAL_IF_NULL(p, "couldn't xrealloc %d bytes", ptr ? "realloc" : "malloc", size);
	return p;
}

void* xmalloc(size_t size)
{
	void* p = xrealloc(NULL, size);
	memset(p, 0, size);
	return p;
}

