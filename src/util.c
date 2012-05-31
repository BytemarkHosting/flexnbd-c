#include <stdarg.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <unistd.h>

#include "util.h"

static pthread_t main_thread;
static int global_debug;

void error_init()
{
	main_thread = pthread_self();
}

void error(int consult_errno, int close_socket, pthread_mutex_t* unlock, const char* format, ...)
{
	va_list argptr;
	
	fprintf(stderr, "*** ");
	
	va_start(argptr, format);
	vfprintf(stderr, format, argptr);
	va_end(argptr);
	
	if (consult_errno) {
		fprintf(stderr, " (errno=%d, %s)", errno, strerror(errno));
	}
	
	if (close_socket)
		close(close_socket);
	
	if (unlock)
		pthread_mutex_unlock(unlock);
	
	fprintf(stderr, "\n");
	
	if (pthread_equal(pthread_self(), main_thread))
		exit(1);
	else
		pthread_exit((void*) 1);
}

void* xrealloc(void* ptr, size_t size)
{
	void* p = realloc(ptr, size);
	if (p == NULL)
		SERVER_ERROR("couldn't xrealloc %d bytes", size);
	return p;
}

void* xmalloc(size_t size)
{
	void* p = xrealloc(NULL, size);
	memset(p, 0, size);
	return p;
}


void set_debug(int value) {
	global_debug = value;
}

#ifdef DEBUG
#  include <sys/times.h>
#  include <stdarg.h>

void debug(const char *msg, ...) {
  va_list argp;

	if ( global_debug ) {
    fprintf(stderr, "%08x %4d: ", (int) pthread_self(), (int) clock() );
    fprintf(stderr, msg, argp);
    fprintf(stderr, "\n");
	}
}
#endif

