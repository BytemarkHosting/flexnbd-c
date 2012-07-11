#ifndef FLEXTHREAD_H
#define FLEXTHREAD_H

#include <pthread.h>

/* Define a mutex wrapper object.  This wrapper allows us to easily
 * track whether or not we currently hold the wrapped mutex.  If we hold
 * the mutex when we destroy it, then we first release it.
 *
 * These are specifically for the case where an ERROR_* handler gets
 * called when we might (or might not) have a mutex held.  The
 * flexthread_mutex_held() function will tell you if your thread
 * currently holds the given mutex.  It's not safe to make any other
 * comparisons.
 */

struct flexthread_mutex {
	pthread_mutex_t mutex;
	pthread_t holder;
};

struct flexthread_mutex * flexthread_mutex_create(void);
void flexthread_mutex_destroy( struct flexthread_mutex * );

int flexthread_mutex_lock( struct flexthread_mutex * );
int flexthread_mutex_unlock( struct flexthread_mutex * );
int flexthread_mutex_held( struct flexthread_mutex * );

#endif
