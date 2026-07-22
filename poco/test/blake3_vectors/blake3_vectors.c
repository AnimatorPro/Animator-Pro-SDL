#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(POCO_BLAKE3_PORTABLE_FIXTURE)
#include <blake3.h>

static void hash_bytes(const void* input, size_t input_size, uint8_t output[32])
{
	blake3_hasher hasher;

	blake3_hasher_init(&hasher);
	if (input_size != 0)
		blake3_hasher_update(&hasher, input, input_size);
	blake3_hasher_finalize(&hasher, output, BLAKE3_OUT_LEN);
}
#else
#include "poco_hash.h"

static void hash_bytes(const void* input, size_t input_size, uint8_t output[32])
{
	po_blake3_hash(input, input_size, output);
}
#endif

static int from_hex_digit(char digit)
{
	if (digit >= '0' && digit <= '9')
		return digit - '0';
	if (digit >= 'a' && digit <= 'f')
		return digit - 'a' + 10;
	return -1;
}

static int expect_hash(const char* name,
					   const void* input,
					   size_t input_size,
					   const char* expected_hex)
{
	uint8_t actual[32];
	uint8_t expected[32];
	size_t i;

	for (i = 0; i < sizeof(expected); ++i) {
		int high = from_hex_digit(expected_hex[i * 2]);
		int low = from_hex_digit(expected_hex[i * 2 + 1]);
		if (high < 0 || low < 0) {
			fprintf(stderr, "%s: malformed expected digest\n", name);
			return 0;
		}
		expected[i] = (uint8_t)((high << 4) | low);
	}

	hash_bytes(input, input_size, actual);
	if (memcmp(actual, expected, sizeof(actual)) != 0) {
		fprintf(stderr, "%s: BLAKE3 digest mismatch\n", name);
		return 0;
	}
	return 1;
}

int main(void)
{
	uint8_t published_input[102400];
	size_t i;

	for (i = 0; i < sizeof(published_input); ++i)
		published_input[i] = (uint8_t)(i % 251);

	if (!expect_hash("empty", NULL, 0,
			"af1349b9f5f9a1a6a0404dea36dcc949"
			"9bcb25c9adc112b7cc9a93cae41f3262") ||
		!expect_hash("published-3", published_input, 3,
			"e1be4d7a8ab5560aa4199eea339849ba"
			"8e293d55ca0a81006726d184519e647f") ||
		!expect_hash("published-102400", published_input,
			sizeof(published_input),
			"bc3e3d41a1146b069abffad3c0d44860"
			"cf664390afce4d9661f7902e7943e085")) {
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
