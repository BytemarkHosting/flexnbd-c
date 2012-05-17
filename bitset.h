#ifndef __BITSET_H
#define __BITSET_H

#include <string.h>

static inline char char_with_bit_set(int num) {
	return 1<<(num%8);
}
static inline int bit_is_set(char* b, int idx) {
	return (b[idx/8] & char_with_bit_set(idx)) != 0;
}
static inline int bit_is_clear(char* b, int idx) {
	return !bit_is_set(b, idx);
}
static inline void bit_set(char* b, int idx) {
	b[idx/8] &= char_with_bit_set(idx);
}
static inline void bit_clear(char* b, int idx) {
	b[idx/8] &= ~char_with_bit_set(idx);
}
static inline void bit_set_range(char* b, int from, int len) {
	for (; from%8 != 0 && len > 0; len--)
		bit_set(b, from++);
	if (len >= 8)
		memset(b+(from/8), 255, len/8);
	for (; len > 0; len--)
		bit_set(b, from++);
}
static inline void bit_clear_range(char* b, int from, int len) {
	for (; from%8 != 0 && len > 0; len--)
		bit_clear(b, from++);
	if (len >= 8)
		memset(b+(from/8), 0, len/8);
	for (; len > 0; len--)
		bit_clear(b, from++);
}

#endif

