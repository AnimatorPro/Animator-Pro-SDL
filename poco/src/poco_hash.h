#ifndef POCO_HASH_H
#define POCO_HASH_H

#include <stddef.h>
#include <stdint.h>

#define POCO_BLAKE3_HASH_SIZE 32u

/*
 * The single byte-image hash used for both source-content addressing and
 * serialized-image integrity. An empty byte sequence may be passed as
 * (NULL, 0); non-empty input must be readable for input_size bytes.
 */
void po_blake3_hash(const void* input,
					size_t input_size,
					uint8_t output[POCO_BLAKE3_HASH_SIZE]);

/*
 * Constant-time comparison of two BLAKE3 digests. Returns non-zero when the
 * digests are equal. Used for both integrity and source-content checks so a
 * mismatch never leaks timing about where the digests first diverge.
 */
int poco_hash_equal(const uint8_t left[POCO_BLAKE3_HASH_SIZE],
					const uint8_t right[POCO_BLAKE3_HASH_SIZE]);

#endif
