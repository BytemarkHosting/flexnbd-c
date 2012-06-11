#ifndef __UTIL_H
#define __UTIL_H

#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>

void* xrealloc(void* ptr, size_t size);
void* xmalloc(size_t size);

typedef void (cleanup_handler)(void* /* context */, int /* is fatal? */);

/* set from 0 - 5 depending on what level of verbosity you want */
extern int log_level;

/* set up the error globals */
void error_init(void);

/* error_set_handler must be a macro not a function due to setjmp stack rules */
#include <setjmp.h>

struct error_handler_context {
	jmp_buf          jmp;
	cleanup_handler* handler;
	void*            data;
};

#define DECLARE_ERROR_CONTEXT(name) \
  struct error_handler_context *name = (struct error_handler_context*) \
  pthread_getspecific(cleanup_handler_key)

/* clean up with the given function & data when error_handler() is invoked,
 * non-fatal errors will also return here (if that's dangerous, use fatal()
 * instead of error()).
 *
 * error handlers are thread-local, so you need to call this when starting a
 * new thread.
 */
extern pthread_key_t cleanup_handler_key;
#define error_set_handler(cleanfn, cleandata) \
{ \
	DECLARE_ERROR_CONTEXT(old); \
	struct error_handler_context *context = \
	  xmalloc(sizeof(struct error_handler_context)); \
	context->handler = (cleanfn); \
	context->data = (cleandata); \
	\
	switch (setjmp(context->jmp)) \
	{ \
	case 0: /* setup time */ \
		if (old) { free(old); }\
		pthread_setspecific(cleanup_handler_key, context); \
		break; \
	case 1: /* fatal error, terminate thread */ \
		context->handler(context->data, 1); \
		pthread_exit((void*) 1); \
		abort(); \
	case 2: /* non-fatal error, return to context of error handler setup */ \
		context->handler(context->data, 0); \
	default: \
		abort(); \
	} \
}


/* invoke the error handler - longjmps away, don't use directly */
void error_handler(int fatal);

/* mylog a line at the given level (0 being most verbose) */
void mylog(int line_level, const char* format, ...);

#ifdef DEBUG
#  define debug(msg, ...) mylog(0, "%s:%d: " msg, __FILE__, __LINE__, ##__VA_ARGS__)
#else
#  define debug(msg, ...) /* no-op */
#endif

/* informational message, not expected to be compiled out */
#define info(msg, ...) mylog(1, "%s:%d: " msg, __FILE__, __LINE__, ##__VA_ARGS__)

/* messages that might indicate a problem */
#define warn(msg, ...) mylog(2, "%s:%d: " msg, __FILE__, __LINE__, ##__VA_ARGS__)

/* mylog a message and invoke the error handler to recover */
#define error(msg, ...) do { \
	mylog(3, "*** %s:%d: " msg, __FILE__, __LINE__, ##__VA_ARGS__); \
	error_handler(0); \
} while(0)

/* mylog a message and invoke the error handler to kill the current thread */
#define fatal(msg, ...) do { \
	mylog(4, "*** %s:%d: " msg, __FILE__, __LINE__, ##__VA_ARGS__); \
	error_handler(1); \
} while(0)

#define ERROR_IF( test, msg, ... ) do { if ((test)) { error(msg, ##__VA_ARGS__); } } while(0)
#define FATAL_IF( test, msg, ... ) do { if ((test)) { fatal(msg, ##__VA_ARGS__); } } while(0)

#define ERROR_UNLESS( test, msg, ... ) ERROR_IF( !(test), msg, ##__VA_ARGS__ )
#define FATAL_UNLESS( test, msg, ... ) FATAL_IF( !(test), msg, ##__VA_ARGS__ )


#define ERROR_IF_NULL(value, msg, ...) \
	ERROR_IF( NULL == value, msg " (errno=%d, %s)", ##__VA_ARGS__, errno, strerror(errno) )
#define FATAL_IF_NULL(value, msg, ...) \
	FATAL_IF( NULL == value, msg " (errno=%d, %s)", ##__VA_ARGS__, errno, strerror(errno) )

#define ERROR_IF_NEGATIVE( value, msg, ... ) ERROR_IF( value < 0, msg, ##__VA_ARGS__ )
#define FATAL_IF_NEGATIVE( value, msg, ... ) FATAL_IF( value < 0, msg, ##__VA_ARGS__ )

#define ERROR_IF_ZERO( value, msg, ... ) ERROR_IF( 0 == value, msg, ##__VA_ARGS__ )
#define FATAL_IF_ZERO( value, msg, ... ) FATAL_IF( 0 == value, msg, ##__VA_ARGS__ )



#define ERROR_UNLESS_NULL(value, msg, ...) \
	ERROR_UNLESS( NULL == value, msg " (errno=%d, %s)", ##__VA_ARGS__, errno, strerror(errno) )
#define FATAL_UNLESS_NULL(value, msg, ...) \
	FATAL_UNLESS( NULL == value, msg " (errno=%d, %s)", ##__VA_ARGS__, errno, strerror(errno) )

#define ERROR_UNLESS_NEGATIVE( value, msg, ... ) ERROR_UNLESS( value < 0, msg, ##__VA_ARGS__ )
#define FATAL_UNLESS_NEGATIVE( value, msg, ... ) FATAL_UNLESS( value < 0, msg, ##__VA_ARGS__ )

#define ERROR_UNLESS_ZERO( value, msg, ... ) ERROR_UNLESS( 0 == value, msg, ##__VA_ARGS__ )
#define FATAL_UNLESS_ZERO( value, msg, ... ) FATAL_UNLESS( 0 == value, msg, ##__VA_ARGS__ )


#define NULLCHECK(value) FATAL_IF_NULL(value, "BUG: " #value " is null")


#endif

