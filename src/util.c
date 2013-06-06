#include <stdarg.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <unistd.h>
#include <time.h>

#include "util.h"

pthread_key_t cleanup_handler_key;

int log_level = 2;

void error_init(void)
{
	pthread_key_create(&cleanup_handler_key, free);
}

void error_handler(int fatal)
{
	DECLARE_ERROR_CONTEXT(context);

	if (context) {
		longjmp(context->jmp, fatal ? 1 : 2 );
	}
	else {
		if ( fatal ) { abort(); }
		else { pthread_exit((void*) 1); }
	}
}


void exit_err( const char *msg )
{
	fprintf( stderr, "%s\n", msg );
	exit( 1 );
}


void mylog(int line_level, const char* format, ...)
{
	if (line_level < log_level) { return; }

	va_list argptr;

	va_start(argptr, format);
	vfprintf(stderr, format, argptr);
	va_end(argptr);
}

uint64_t monotonic_time_ms()
{
	struct timespec ts;
	uint64_t seconds_ms, nanoseconds_ms;

	FATAL_IF_NEGATIVE(
		clock_gettime(CLOCK_MONOTONIC, &ts),
		SHOW_ERRNO( "clock_gettime failed" )
	);

	seconds_ms = ts.tv_sec;
	seconds_ms = seconds_ms * 1000;

	nanoseconds_ms = ts.tv_nsec;
	nanoseconds_ms = nanoseconds_ms / 1000000;

	return seconds_ms + nanoseconds_ms;
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

