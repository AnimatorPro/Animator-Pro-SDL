#include "poco_hash.h"

#include <blake3.h>

void po_blake3_hash(const void* input,
					size_t input_size,
					uint8_t output[POCO_BLAKE3_HASH_SIZE])
{
	blake3_hasher hasher;

	blake3_hasher_init(&hasher);
	if (input_size != 0)
		blake3_hasher_update(&hasher, input, input_size);
	blake3_hasher_finalize(&hasher, output, POCO_BLAKE3_HASH_SIZE);
}

int poco_hash_equal(const uint8_t left[POCO_BLAKE3_HASH_SIZE],
					const uint8_t right[POCO_BLAKE3_HASH_SIZE])
{
	uint8_t difference = 0;
	size_t index;

	for (index = 0; index < POCO_BLAKE3_HASH_SIZE; ++index) {
		difference |= (uint8_t)(left[index] ^ right[index]);
	}
	return difference == 0;
}
