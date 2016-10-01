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
#include <stdint.h>
#include "fifo_declare.h"

typedef union {
	uint64_t i;
	void * p;
} mbox_item_t;

DECLARE_FIFO(mbox_item_t, mbox_fifo, 8);

typedef struct mbox_t {
	mbox_fifo_t	fifo;
	// socketpair() ends
	int		signalw, signalr;
} mbox_t, *mbox_p;


/* Create an mbox_t. */
mbox_p  mbox_create(void);

/* Put something in the mbox, blocking if it's already full.
 * That something can be NULL if you want.
 */
void mbox_post( mbox_p , mbox_item_t item);

/* See what's in the mbox.  This isn't thread-safe. */
mbox_item_t mbox_contents( mbox_p );

/* See if anything has been put into the mbox.  This isn't thread-safe.
 * */
int mbox_is_full( mbox_p );

/* Get the contents from the mbox, blocking if there's nothing there. */
mbox_item_t mbox_receive( mbox_p );

/* Free the mbox and destroy the associated pthread bits. */
void mbox_destroy( mbox_p );


#endif
