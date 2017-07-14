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


#endif

