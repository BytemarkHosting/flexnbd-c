#ifndef BITSET_H
#define BITSET_H

#include "util.h"

#include <inttypes.h>
#include <string.h>
#include <pthread.h>


static inline char char_with_bit_set(int num) { return 1<<(num%8); }

/** Return 1 if the bit at ''idx'' in array ''b'' is set */
static inline int bit_is_set(char* b, int idx) {
	return (b[idx/8] & char_with_bit_set(idx)) != 0;
}
/** Return 1 if the bit at ''idx'' in array ''b'' is clear */
static inline int bit_is_clear(char* b, int idx) {
	return !bit_is_set(b, idx);
}
/** Tests whether the bit at ''idx'' in array ''b'' has value ''value'' */
static inline int bit_has_value(char* b, int idx, int value) {
	if (value) { return bit_is_set(b, idx); }
	else { return bit_is_clear(b, idx); }
}
/** Sets the bit ''idx'' in array ''b'' */
static inline void bit_set(char* b, int idx) {
	b[idx/8] |= char_with_bit_set(idx);
	//__sync_fetch_and_or(b+(idx/8), char_with_bit_set(idx));
}
/** Clears the bit ''idx'' in array ''b'' */
static inline void bit_clear(char* b, int idx) {
	b[idx/8] &= ~char_with_bit_set(idx);
	//__sync_fetch_and_nand(b+(idx/8), char_with_bit_set(idx));
}
/** Sets ''len'' bits in array ''b'' starting at offset ''from'' */
static inline void bit_set_range(char* b, int from, int len) {
	for (; from%8 != 0 && len > 0; len--) { bit_set(b, from++); }
	if (len >= 8) { memset(b+(from/8), 255, len/8); }
	for (; len > 0; len--) { bit_set(b, from++); }
}
/** Clears ''len'' bits in array ''b'' starting at offset ''from'' */
static inline void bit_clear_range(char* b, int from, int len) {
	for (; from%8 != 0 && len > 0; len--) {	bit_clear(b, from++); }
	if (len >= 8) { memset(b+(from/8), 0, len/8); }
	for (; len > 0; len--) { bit_clear(b, from++); }
}

/** Counts the number of contiguous bits in array ''b'', starting at ''from'' 
  * up to a maximum number of bits ''len''.  Returns the number of contiguous
  * bits that are the same as the first one specified.
  */
static inline int bit_run_count(char* b, int from, int len) {
	int count;
	int first_value = bit_is_set(b, from);

	for (count=0; len > 0 && bit_has_value(b, from+count, first_value); count++, len--)
		;

	/* FIXME: debug this later */
	/*for (; (from+count) % 64 != 0 && len > 0; len--)
		if (bit_has_value(b, from+count, first_value))
			count++;
		else
			return count;
	for (; len >= 64; len-=64) {
		if (*((uint64_t*)(b + ((from+count)/8))) == UINT64_MAX)
			count += 64;
		else
			break;
	}
	for (; len > 0; len--)
		if (bit_is_set(b, from+count))
			count++;*/

	return count;
}

/** An application of a bitset - a bitset mapping represents a file of ''size'' 
  * broken down into ''resolution''-sized chunks.  The bit set is assumed to
  * represent one bit per chunk.
  */
struct bitset_mapping {
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
	return bitset;
}

#define INT_FIRST_AND_LAST \
  int first = from/set->resolution, \
      last = (from+len-1)/set->resolution, \
      bitlen = last-first+1

/** Set the bits in a bitset which correspond to the given bytes in the larger
  * file.
  */
static inline void bitset_set_range(
	struct bitset_mapping* set, 
	uint64_t from, 
	uint64_t len)
{
	INT_FIRST_AND_LAST;
	bit_set_range(set->bits, first, bitlen);
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
	bit_clear_range(set->bits, first, bitlen);
}


/** Clear every bit in the bitset. */
static inline void bitset_clear(
		struct bitset_mapping *set
)
{
	bitset_clear_range(set, 0, set->size);
}


/** Counts the number of contiguous bytes that are represented as a run in
  * the bit field.
  */
static inline int bitset_run_count(
	struct bitset_mapping* set, 
	uint64_t from, 
	uint64_t len)
{
	/* now fix in case len goes past the end of the memory we have
	 * control of */
	len = len+from>set->size ? set->size-from : len;
	INT_FIRST_AND_LAST;
	return (bit_run_count(set->bits, first, bitlen) * set->resolution) -
	  (from % set->resolution);
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

