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

void error_init()
{
	main_thread = pthread_self();
}

void error(int consult_errno, int close_socket, const char* format, ...)
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

