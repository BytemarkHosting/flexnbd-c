
#include "mbox.h"
#include "util.h"

#include <sys/socket.h>
#include <pthread.h>

DEFINE_FIFO(mbox_item_t, mbox_fifo);

#define ARRAY_SIZE(w) (sizeof(w) / sizeof((w)[0]))

mbox_p  mbox_create( void )
{
	mbox_p  mbox = xmalloc( sizeof( struct mbox_t ) );

	int sv[2];
	FATAL_UNLESS(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0,
			"Failed to socketpair");
	mbox->signalw = sv[0];
	mbox->signalr = sv[1];

	return mbox;
}

void mbox_post( mbox_p  mbox, mbox_item_t item )
{
	mbox_fifo_write(&mbox->fifo, item);
	{
		uint8_t w;
		FATAL_UNLESS((write(mbox->signalw, &w, 1)) == 1,
				"Write to socketpair");
	}
}


mbox_item_t mbox_contents( mbox_p  mbox )
{
	const mbox_item_t zero = {0};

	return mbox_fifo_isempty(&mbox->fifo) ?
				zero :
				mbox_fifo_read_at(&mbox->fifo, 0);
}


int mbox_is_full( mbox_p  mbox )
{
	return mbox_fifo_isfull(&mbox->fifo);
}


mbox_item_t mbox_receive( mbox_p  mbox )
{
	NULLCHECK( mbox );

	while (mbox_fifo_isempty(&mbox->fifo)) {
		uint8_t w;
		FATAL_UNLESS((read(mbox->signalr, &w, 1)) == 1,
				"Read from socketpair");
	}

	return mbox_fifo_read(&mbox->fifo);
}


void mbox_destroy( mbox_p  mbox )
{
	NULLCHECK( mbox );

	close(mbox->signalw);
	close(mbox->signalr);
	free( mbox );
}
