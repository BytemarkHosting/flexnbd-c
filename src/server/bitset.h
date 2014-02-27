#ifndef BITSET_H
#define BITSET_H

#include "util.h"

#include <inttypes.h>
#include <string.h>
#include <pthread.h>

/*
 * Make the bitfield words 'opaque' to prevent code
 * poking at the bits directly without using these
 * accessors/macros
 */
typedef uint64_t	bitfield_word_t;
typedef bitfield_word_t * bitfield_p;

#define BITFIELD_WORD_SIZE	sizeof(bitfield_word_t)
#define BITS_PER_WORD	(BITFIELD_WORD_SIZE * 8)

#define BIT_MASK(_idx)	\
			(1LL << ((_idx) & (BITS_PER_WORD - 1)))
#define BIT_WORD(_b, _idx)	\
			((bitfield_word_t*)(_b))[(_idx) / BITS_PER_WORD]

/* Calculates the number of words needed to store _bytes number of bytes
 * this is added to accommodate code that wants to use bytes sizes
 */
#define BIT_WORDS_FOR_SIZE(_bytes) \
			((_bytes + (BITFIELD_WORD_SIZE-1)) / BITFIELD_WORD_SIZE)

/** Return the bit value ''idx'' in array ''b'' */
static inline int bit_get(bitfield_p b, uint64_t idx) {
	return (BIT_WORD(b, idx) >> (idx & (BITS_PER_WORD-1))) & 1;
}

/** Return 1 if the bit at ''idx'' in array ''b'' is set */
static inline int bit_is_set(bitfield_p b, uint64_t idx) {
	return bit_get(b, idx);
}
/** Return 1 if the bit at ''idx'' in array ''b'' is clear */
static inline int bit_is_clear(bitfield_p b, uint64_t idx) {
	return !bit_get(b, idx);
}
/** Tests whether the bit at ''idx'' in array ''b'' has value ''value'' */
static inline int bit_has_value(bitfield_p b, uint64_t idx, int value) {
	return bit_get(b, idx) == !!value;
}
/** Sets the bit ''idx'' in array ''b'' */
static inline void bit_set(bitfield_p b, uint64_t idx) {
	BIT_WORD(b, idx) |= BIT_MASK(idx);
}
/** Clears the bit ''idx'' in array ''b'' */
static inline void bit_clear(bitfield_p b, uint64_t idx) {
	BIT_WORD(b, idx) &= ~BIT_MASK(idx);
}
/** Sets ''len'' bits in array ''b'' starting at offset ''from'' */
static inline void bit_set_range(bitfield_p b, uint64_t from, uint64_t len)
{
	for ( ; (from % BITS_PER_WORD) != 0 && len > 0 ; len-- ) {
		bit_set( b, from++ );
	}

	if (len >= BITS_PER_WORD) {
		memset(&BIT_WORD(b, from), 0xff, len / 8 );
		from += len;
		len = len % BITS_PER_WORD;
		from -= len;
	}

	for ( ; len > 0 ; len-- ) {
		bit_set( b, from++ );
	}
}
/** Clears ''len'' bits in array ''b'' starting at offset ''from'' */
static inline void bit_clear_range(bitfield_p b, uint64_t from, uint64_t len)
{
	for ( ; (from % BITS_PER_WORD) != 0 && len > 0 ; len-- ) {
		bit_clear( b, from++ );
	}

	if (len >= BITS_PER_WORD) {
		memset(&BIT_WORD(b, from), 0, len / 8 );
		from += len;
		len = len % BITS_PER_WORD;
		from -= len;
	}

	for ( ; len > 0 ; len-- ) {
		bit_clear( b, from++ );
	}
}

/** Counts the number of contiguous bits in array ''b'', starting at ''from''
  * up to a maximum number of bits ''len''.  Returns the number of contiguous
  * bits that are the same as the first one specified. If ''run_is_set'' is
  * non-NULL, the value of that bit is placed into it.
  */
static inline uint64_t bit_run_count(bitfield_p b, uint64_t from, uint64_t len, int *run_is_set) {
	uint64_t count = 0;
	int first_value = bit_get(b, from);
	bitfield_word_t word_match = first_value ? -1 : 0;

	if ( run_is_set != NULL ) {
		*run_is_set = first_value;
	}

	for ( ; ((from + count) % BITS_PER_WORD) != 0 && len > 0; len--) {
		if (bit_has_value(b, from + count, first_value)) {
			count++;
		} else {
			return count;
		}
	}

	for ( ; len >= BITS_PER_WORD ; len -= BITS_PER_WORD ) {
		if (BIT_WORD(b, from + count) == word_match) {
			count += BITS_PER_WORD;
		} else {
			break;
		}
	}

	for ( ; len > 0; len-- ) {
		if ( bit_has_value(b, from + count, first_value) ) {
			count++;
		}
	}

	return count;
}

enum bitset_stream_events {
  BITSET_STREAM_UNSET = 0,
  BITSET_STREAM_SET = 1,
  BITSET_STREAM_ON = 2,
  BITSET_STREAM_OFF = 3
};
#define BITSET_STREAM_EVENTS_ENUM_SIZE 4

struct bitset_stream_entry {
	enum bitset_stream_events event;
	uint64_t from;
	uint64_t len;
};

/** Limit the stream size to 1MB for now.
 *
 *  If this is too small, it'll cause requests to stall as the migration lags
 *  behind the changes made by those requests.
 */
#define BITSET_STREAM_SIZE ( ( 1024 * 1024 ) / sizeof( struct bitset_stream_entry ) )

struct bitset_stream {
	struct bitset_stream_entry entries[BITSET_STREAM_SIZE];
	int in;
	int out;
	int size;
	pthread_mutex_t mutex;
	pthread_cond_t cond_not_full;
	pthread_cond_t cond_not_empty;
	uint64_t queued_bytes[BITSET_STREAM_EVENTS_ENUM_SIZE];
};


/** An application of a bitset - a bitset mapping represents a file of ''size''
  * broken down into ''resolution''-sized chunks.  The bit set is assumed to
  * represent one bit per chunk.  We also bundle a lock so that the set can be
  * written reliably by multiple threads.
  */
struct bitset {
	pthread_mutex_t lock;
	uint64_t size;
	int resolution;
	struct bitset_stream *stream;
	int stream_enabled;
	bitfield_word_t bits[];
};

/** Allocate a bitset for a file of the given size, and chunks of the
  * given resolution.
  */
static inline struct bitset *bitset_alloc( uint64_t size, int resolution )
{
	// calculate a size to allocate that is a multiple of the size of the
	// bitfield word
	size_t bitfield_size =
			BIT_WORDS_FOR_SIZE((( size + resolution - 1 ) / resolution)) * sizeof( bitfield_word_t );
	struct bitset *bitset = xmalloc(sizeof( struct bitset ) + bitfield_size);

	bitset->size = size;
	bitset->resolution = resolution;
	/* don't actually need to call pthread_mutex_destroy '*/
	pthread_mutex_init(&bitset->lock, NULL);
	bitset->stream = xmalloc( sizeof( struct bitset_stream ) );
	pthread_mutex_init( &bitset->stream->mutex, NULL );

	/* Technically don't need to call pthread_cond_destroy either */
	pthread_cond_init( &bitset->stream->cond_not_full, NULL );
	pthread_cond_init( &bitset->stream->cond_not_empty, NULL );

	return bitset;
}

static inline void bitset_free( struct bitset * set )
{
	/* TODO: free our mutex... */

	free( set->stream );
	set->stream = NULL;

	free( set );
}

#define INT_FIRST_AND_LAST \
  uint64_t first = from/set->resolution, \
      last = ((from+len)-1)/set->resolution, \
      bitlen = (last-first)+1

#define BITSET_LOCK \
  FATAL_IF_NEGATIVE(pthread_mutex_lock(&set->lock), "Error locking bitset")

#define BITSET_UNLOCK \
  FATAL_IF_NEGATIVE(pthread_mutex_unlock(&set->lock), "Error unlocking bitset")


static inline void bitset_stream_enqueue(
	struct bitset * set,
	enum bitset_stream_events event,
	uint64_t from,
	uint64_t len
)
{
	struct bitset_stream * stream = set->stream;

	pthread_mutex_lock( &stream->mutex );

	while ( stream->size == BITSET_STREAM_SIZE ) {
		pthread_cond_wait( &stream->cond_not_full, &stream->mutex );
	}

	stream->entries[stream->in].event = event;
	stream->entries[stream->in].from = from;
	stream->entries[stream->in].len = len;
	stream->queued_bytes[event] += len;

	stream->size++;
	stream->in++;
	stream->in %= BITSET_STREAM_SIZE;

	pthread_mutex_unlock( & stream->mutex );
	pthread_cond_signal( &stream->cond_not_empty );

	return;
}

static inline void bitset_stream_dequeue(
	struct bitset * set,
	struct bitset_stream_entry * out
)
{
	struct bitset_stream * stream = set->stream;
	struct bitset_stream_entry * dequeued;

	pthread_mutex_lock( &stream->mutex );

	while ( stream->size == 0 ) {
		pthread_cond_wait( &stream->cond_not_empty, &stream->mutex );
	}

	dequeued = &stream->entries[stream->out];

	if ( out != NULL ) {
		out->event = dequeued->event;
		out->from  = dequeued->from;
		out->len   = dequeued->len;
	}

	stream->queued_bytes[dequeued->event] -= dequeued->len;
	stream->size--;
	stream->out++;
	stream->out %= BITSET_STREAM_SIZE;

	pthread_mutex_unlock( &stream->mutex );
	pthread_cond_signal( &stream->cond_not_full );

	return;
}

static inline size_t bitset_stream_size( struct bitset * set )
{
	size_t size;

	pthread_mutex_lock( &set->stream->mutex );
	size = set->stream->size;
	pthread_mutex_unlock( &set->stream->mutex );

	return size;
}

static inline uint64_t bitset_stream_queued_bytes(
	struct bitset * set,
	enum bitset_stream_events event
)
{
	uint64_t total;

	pthread_mutex_lock( &set->stream->mutex );
	total = set->stream->queued_bytes[event];
	pthread_mutex_unlock( &set->stream->mutex );

	return total;
}

static inline void bitset_enable_stream( struct bitset * set )
{
	BITSET_LOCK;
	set->stream_enabled = 1;
	bitset_stream_enqueue( set, BITSET_STREAM_ON, 0, set->size );
	BITSET_UNLOCK;
}

static inline void bitset_disable_stream( struct bitset * set  )
{
	BITSET_LOCK;
	bitset_stream_enqueue( set, BITSET_STREAM_OFF, 0, set->size );
	set->stream_enabled = 0;
	BITSET_UNLOCK;
}

/** Set the bits in a bitset which correspond to the given bytes in the larger
  * file.
  */
static inline void bitset_set_range(
	struct bitset * set,
	uint64_t from,
	uint64_t len)
{
	INT_FIRST_AND_LAST;
	BITSET_LOCK;
	bit_set_range(set->bits, first, bitlen);

	if ( set->stream_enabled ) {
		bitset_stream_enqueue( set, BITSET_STREAM_SET, from, len );
	}

	BITSET_UNLOCK;
}


/** Set every bit in the bitset. */
static inline void bitset_set( struct bitset * set )
{
	bitset_set_range(set, 0, set->size);
}

/** Clear the bits in a bitset which correspond to the given bytes in the
  * larger file.
  */
static inline void bitset_clear_range(
	struct bitset * set,
	uint64_t from,
	uint64_t len)
{
	INT_FIRST_AND_LAST;
	BITSET_LOCK;
	bit_clear_range(set->bits, first, bitlen);

	if ( set->stream_enabled ) {
		bitset_stream_enqueue( set, BITSET_STREAM_UNSET, from, len );
	}

	BITSET_UNLOCK;
}


/** Clear every bit in the bitset. */
static inline void bitset_clear( struct bitset * set )
{
	bitset_clear_range(set, 0, set->size);
}

/** As per bitset_run_count but also tells you whether the run it found was set
  * or unset, atomically.
  */
static inline uint64_t bitset_run_count_ex(
	struct bitset * set,
	uint64_t from,
	uint64_t len,
	int* run_is_set
)
{
	uint64_t run;

	/* Clip our requests to the end of the bitset,  avoiding uint underflow. */
	if ( from > set->size ) {
		return 0;
	}
	len = ( len + from ) > set->size ? ( set->size - from ) : len;

	INT_FIRST_AND_LAST;

	BITSET_LOCK;
	run = bit_run_count(set->bits, first, bitlen, run_is_set) * set->resolution;
	run -= (from % set->resolution);
	BITSET_UNLOCK;

	return run;
}

/** Counts the number of contiguous bytes that are represented as a run in
  * the bit field.
  */
static inline uint64_t bitset_run_count(
	struct bitset * set,
	uint64_t from,
	uint64_t len)
{
	return bitset_run_count_ex( set, from, len, NULL );
}

/** Tests whether the bit field is clear for the given file offset.
  */
static inline int bitset_is_clear_at( struct bitset * set, uint64_t at )
{
	return bit_is_clear(set->bits, at/set->resolution);
}

/** Tests whether the bit field is set for the given file offset.
  */
static inline int bitset_is_set_at( struct bitset * set, uint64_t at )
{
	return bit_is_set(set->bits, at/set->resolution);
}


#endif

