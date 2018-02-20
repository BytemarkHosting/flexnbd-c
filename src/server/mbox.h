#ifndef MBOX_H
#define MBOX_H

/** mbox
 * A thread sync object.  Put a void * into the mbox in one thread, and
 * get it out in another.  The receiving thread will block if there's
 * nothing in the mbox, and the sending thread will block if there is.
 * The mbox doesn't assume any responsibility for the pointer it's
 * passed - you must free it yourself if it's malloced.
 */


#include <pthread.h>


struct mbox {
    void *contents;

	/** Marker to tell us if there's content in the box.
	 * Keeping this separate allows us to use NULL for the contents.
	 */
    int full;

	/** This gets signaled by mbox_post, and waited on by
	 * mbox_receive */
    pthread_cond_t filled_cond;
	/** This is signaled by mbox_receive, and waited on by mbox_post */
    pthread_cond_t emptied_cond;
    pthread_mutex_t mutex;
};


/* Create an mbox. */
struct mbox *mbox_create(void);

/* Put something in the mbox, blocking if it's already full.
 * That something can be NULL if you want.
 */
void mbox_post(struct mbox *, void *);

/* See what's in the mbox.  This isn't thread-safe. */
void *mbox_contents(struct mbox *);

/* See if anything has been put into the mbox.  This isn't thread-safe.
 * */
int mbox_is_full(struct mbox *);

/* Get the contents from the mbox, blocking if there's nothing there. */
void *mbox_receive(struct mbox *);

/* Free the mbox and destroy the associated pthread bits. */
void mbox_destroy(struct mbox *);


#endif
