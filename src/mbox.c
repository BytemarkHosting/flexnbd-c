#include "mbox.h"
#include "util.h"

#include <pthread.h>

struct mbox * mbox_create( void )
{
	struct mbox * mbox = xmalloc( sizeof( struct mbox ) );
	FATAL_UNLESS( 0 == pthread_cond_init( &mbox->filled_cond, NULL ),
			"Failed to initialise a condition variable" );
	FATAL_UNLESS( 0 == pthread_cond_init( &mbox->emptied_cond, NULL ),
			"Failed to initialise a condition variable" );
	FATAL_UNLESS( 0 == pthread_mutex_init( &mbox->mutex, NULL ),
			"Failed to initialise a mutex" );
	return mbox;
}

void mbox_post( struct mbox * mbox, void * contents )
{
	pthread_mutex_lock( &mbox->mutex );
	{
		if (mbox->full){
			pthread_cond_wait( &mbox->emptied_cond, &mbox->mutex );
		}
		mbox->contents = contents;
		mbox->full = 1;
		while( 0 != pthread_cond_signal( &mbox->filled_cond ) );
	}
	pthread_mutex_unlock( &mbox->mutex );
}


void * mbox_contents( struct mbox * mbox )
{
	return mbox->contents;
}


int mbox_is_full( struct mbox * mbox )
{
	return mbox->full;
}


void * mbox_receive( struct mbox * mbox )
{
	NULLCHECK( mbox );
	void * result;

	pthread_mutex_lock( &mbox->mutex );
	{
		if ( !mbox->full ) {
			pthread_cond_wait( &mbox->filled_cond, &mbox->mutex );
		}
		mbox->full = 0;
		result = mbox->contents;
		mbox->contents = NULL;
		while( 0 != pthread_cond_signal( &mbox->emptied_cond));
	}
	pthread_mutex_unlock( &mbox->mutex );


	return result;
}


void mbox_destroy( struct mbox * mbox )
{
	NULLCHECK( mbox );

	while( 0 != pthread_cond_destroy( &mbox->emptied_cond ) );
	while( 0 != pthread_cond_destroy( &mbox->filled_cond ) );

	while( 0 != pthread_mutex_destroy( &mbox->mutex ) );

	free( mbox );
}
