#ifndef BITSET_H
#define BITSET_H

#include "util.h"

#include <inttypes.h>
#include <string.h>
#include <pthread.h>


static inline char char_with_bit_set(uint64_t num) { return 1<<(num%8); }

/** Return 1 if the bit at ''idx'' in array ''b'' is set */
static inline int bit_is_set(char* b, uint64_t idx) {
	return (b[idx/8] & char_with_bit_set(idx)) != 0;
}
/** Return 1 if the bit at ''idx'' in array ''b'' is clear */
static inline int bit_is_clear(char* b, uint64_t idx) {
	return !bit_is_set(b, idx);
}
/** Tests whether the bit at ''idx'' in array ''b'' has value ''value'' */
static inline int bit_has_value(char* b, uint64_t idx, int value) {
	if (value) { return bit_is_set(b, idx); }
	else { return bit_is_clear(b, idx); }
}
/** Sets the bit ''idx'' in array ''b'' */
static inline void bit_set(char* b, uint64_t idx) {
	b[idx/8] |= char_with_bit_set(idx);
	//__sync_fetch_and_or(b+(idx/8), char_with_bit_set(idx));
}
/** Clears the bit ''idx'' in array ''b'' */
static inline void bit_clear(char* b, uint64_t idx) {
	b[idx/8] &= ~char_with_bit_set(idx);
	//__sync_fetch_and_nand(b+(idx/8), char_with_bit_set(idx));
}
/** Sets ''len'' bits in array ''b'' starting at offset ''from'' */
static inline void bit_set_range(char* b, uint64_t from, uint64_t len)
{
	for ( ; from%8 != 0 && len > 0 ; len-- ) {
		bit_set( b, from++ );
	}

	if (len >= 8) {
		memset(b+(from/8), 255, len/8 );
		from += len;
		len = (len%8);
		from -= len;
	}

	for ( ; len > 0 ; len-- ) {
		bit_set( b, from++ );
	}
}
/** Clears ''len'' bits in array ''b'' starting at offset ''from'' */
static inline void bit_clear_range(char* b, uint64_t from, uint64_t len)
{
	for ( ; from%8 != 0 && len > 0 ; len-- ) {
		bit_clear( b, from++ );
	}

	if (len >= 8) {
		memset(b+(from/8), 0, len/8 );
		from += len;
		len = (len%8);
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
static inline uint64_t bit_run_count(char* b, uint64_t from, uint64_t len, int *run_is_set) {
	uint64_t* current_block;
	uint64_t count = 0;
	int first_value = bit_is_set(b, from);

	if ( run_is_set != NULL ) {
		*run_is_set = first_value;
	}

	for ( ; (from+count) % 64 != 0 && len > 0; len--) {
		if (bit_has_value(b, from+count, first_value)) {
			count++;
		} else {
			return count;
		}
	}

	for ( ; len >= 64 ; len -= 64 ) {
		current_block = (uint64_t*) (b + ((from+count)/8));
		if (*current_block == ( first_value ? UINT64_MAX : 0 ) ) {
			count += 64;
		} else {
			break;
		}
	}

	for ( ; len > 0; len-- ) {
		if ( bit_has_value(b, from+count, first_value) ) {
			count++;
		}
	}

	return count;
}

/** An application of a bitset - a bitset mapping represents a file of ''size''
  * broken down into ''resolution''-sized chunks.  The bit set is assumed to
  * represent one bit per chunk.  We also bundle a lock so that the set can be
  * written reliably by multiple threads.
  */
struct bitset_mapping {
	pthread_mutex_t lock;
	uint64_t size;
	int resolution;
	char bits[];
};

/** Allocate a bitset_mapping for a file of the given size, and chunks of the
  * given resolution.
  */
static inline struct bitset_mapping* bitset_alloc(
	uint64_t size,
	int resolution
)
{
	struct bitset_mapping *bitset = xmalloc(
		sizeof(struct bitset_mapping)+
		(size+resolution-1)/resolution
	);
	bitset->size = size;
	bitset->resolution = resolution;
	/* don't actually need to call pthread_mutex_destroy '*/
	pthread_mutex_init(&bitset->lock, NULL);
	return bitset;
}

#define INT_FIRST_AND_LAST \
  uint64_t first = from/set->resolution, \
      last = ((from+len)-1)/set->resolution, \
      bitlen = (last-first)+1

#define BITSET_LOCK \
  FATAL_IF_NEGATIVE(pthread_mutex_lock(&set->lock), "Error locking bitset")

#define BITSET_UNLOCK \
  FATAL_IF_NEGATIVE(pthread_mutex_unlock(&set->lock), "Error unlocking bitset")


/** Set the bits in a bitset which correspond to the given bytes in the larger
  * file.
  */
static inline void bitset_set_range(
	struct bitset_mapping* set,
	uint64_t from,
	uint64_t len)
{
	INT_FIRST_AND_LAST;
	BITSET_LOCK;
	bit_set_range(set->bits, first, bitlen);
	BITSET_UNLOCK;
}


/** Set every bit in the bitset. */
static inline void bitset_set(
	struct bitset_mapping* set
)
{
	bitset_set_range(set, 0, set->size);
}



/** Clear the bits in a bitset which correspond to the given bytes in the
  * larger file.
  */
static inline void bitset_clear_range(
	struct bitset_mapping* set,
	uint64_t from,
	uint64_t len)
{
	INT_FIRST_AND_LAST;
	BITSET_LOCK;
	bit_clear_range(set->bits, first, bitlen);
	BITSET_UNLOCK;
}


/** Clear every bit in the bitset. */
static inline void bitset_clear(
		struct bitset_mapping *set
)
{
	bitset_clear_range(set, 0, set->size);
}

/** As per bitset_run_count but also tells you whether the run it found was set
  * or unset, atomically.
  */
static inline uint64_t bitset_run_count_ex(
	struct bitset_mapping* set,
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
	struct bitset_mapping* set,
	uint64_t from,
	uint64_t len)
{
	return bitset_run_count_ex( set, from, len, NULL );
}

/** Tests whether the bit field is clear for the given file offset.
  */
static inline int bitset_is_clear_at(
	struct bitset_mapping* set,
	uint64_t at
)
{
	return bit_is_clear(set->bits, at/set->resolution);
}

/** Tests whether the bit field is set for the given file offset.
  */
static inline int bitset_is_set_at(
	struct bitset_mapping* set,
	uint64_t at
)
{
	return bit_is_set(set->bits, at/set->resolution);
}


#endif

