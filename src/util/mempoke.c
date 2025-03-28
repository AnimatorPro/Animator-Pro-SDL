/* mempoke.c */

#include <string.h>

#include "memory.h"

/* Function: pj_stuff_bytes */
void pj_stuff_bytes(uint8_t data, void *dst, unsigned int n)
{
	memset(dst, data, n);
}

/* Function: pj_stuff_words */
void pj_stuff_words(uint16_t data, void *dst, unsigned int n)
{
	uint16_t *dest = dst;
	unsigned int i;

	for (i = 0; i < n; i++) {
		dest[i] = data;
	}
}

/* Function: pj_stuff_dwords */
void pj_stuff_dwords(uint32_t data, void *dst, unsigned int n)
{
	uint32_t *dest = dst;
	unsigned int i;

	for (i = 0; i < n; i++) {
		dest[i] = data;
	}
}

/* Function: pj_stuff_pointers */
void pj_stuff_pointers(void *data, void *dst, unsigned int n)
{
	void **dest = dst;
	unsigned int i;

	for (i = 0; i < n; i++) {
		dest[i] = data;
	}
}

/* Function: pj_copy_bytes */
void pj_copy_bytes(const void *src, void *dst, unsigned int n)
{
	memmove(dst, src, n);
}

/* Function: pj_copy_words */
void pj_copy_words(const void *src, void *dst, unsigned int n)
{
	memmove(dst, src, 2 * n);
}

/* Function: pj_copy_structure */
void pj_copy_structure(const void *src, void *dst, unsigned int n)
{
	memmove(dst, src, n);
}

/* Function: pj_xor_bytes */
void pj_xor_bytes(uint8_t data, void *dst, unsigned int n)
{
	uint8_t *dest = dst;
	unsigned int i;

	for (i = 0; i < n; i++) {
		dest[i] ^= data;
	}
}

/* Function: pj_xlate
 *
 *  table -> 256 byte translation table
 *  buf -> area of count bytes to translate
 */
void pj_xlate(const uint8_t *table, uint8_t *xs, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; i++) {
		xs[i] = table[xs[i]];
	}
}
