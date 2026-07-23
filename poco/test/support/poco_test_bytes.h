#ifndef POCO_TEST_BYTES_H
#define POCO_TEST_BYTES_H

#include <stdint.h>

/*
 * Little-endian integer helpers for serialization tests. These are kept
 * deliberately independent of poco/src/poco_endian.h so the tests decode and
 * hand-build serialized bytes with a second implementation rather than the one
 * under test.
 */

static inline uint32_t read_u32(const uint8_t* data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
		   ((uint32_t)data[3] << 24);
}

static inline uint64_t read_u64(const uint8_t* data)
{
	uint64_t value = 0;
	unsigned int index;
	for (index = 0; index < 8; ++index) {
		value |= (uint64_t)data[index] << (index * 8);
	}
	return value;
}

static inline void write_u32(uint8_t* data, uint32_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
	data[2] = (uint8_t)(value >> 16);
	data[3] = (uint8_t)(value >> 24);
}

static inline void write_u64(uint8_t* data, uint64_t value)
{
	unsigned int index;
	for (index = 0; index < 8; ++index) {
		data[index] = (uint8_t)(value >> (index * 8));
	}
}

#endif
