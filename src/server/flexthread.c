#include "flexthread.h"
#include "util.h"

#include <pthread.h>


struct flexthread_mutex *flexthread_mutex_create(void)
{
    struct flexthread_mutex *ftm =
	xmalloc(sizeof(struct flexthread_mutex));

    FATAL_UNLESS(0 == pthread_mutex_init(&ftm->mutex, NULL),
		 "Mutex initialisation failed");
    return ftm;

}


void flexthread_mutex_destroy(struct flexthread_mutex *ftm)
{
    NULLCHECK(ftm);

    if (flexthread_mutex_held(ftm)) {
	flexthread_mutex_unlock(ftm);
    } else if ((pthread_t) NULL != ftm->holder) {
	/* This "should never happen": if we can try to destroy
	 * a mutex currently held by another thread, there's a
	 * logic bug somewhere.  I know the test here is racy,
	 * but there's not a lot we can do about it at this
	 * point.
	 */
	fatal("Attempted to destroy a flexthread_mutex"
	      " held by another thread!");
    }

    FATAL_UNLESS(0 == pthread_mutex_destroy(&ftm->mutex),
		 "Mutex destroy failed");
    free(ftm);
}


int flexthread_mutex_lock(struct flexthread_mutex *ftm)
{
    NULLCHECK(ftm);

    int failure = pthread_mutex_lock(&ftm->mutex);
    if (0 == failure) {
	ftm->holder = pthread_self();
    }

    return failure;
}


int flexthread_mutex_unlock(struct flexthread_mutex *ftm)
{
    NULLCHECK(ftm);

    pthread_t orig = ftm->holder;
    ftm->holder = (pthread_t) NULL;
    int failure = pthread_mutex_unlock(&ftm->mutex);
    if (0 != failure) {
	ftm->holder = orig;
    }
    return failure;
}


int flexthread_mutex_held(struct flexthread_mutex *ftm)
{
    NULLCHECK(ftm);
    return pthread_self() == ftm->holder;
}
