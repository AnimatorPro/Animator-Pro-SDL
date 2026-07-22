#ifndef POCO_ENDIAN_H
#define POCO_ENDIAN_H

#include <stddef.h>
#include <stdint.h>

/*
 * Fixed little-endian integer codec shared by every serialized poco format
 * (bytecode envelope, archive header, program image). Encoding is independent
 * of host byte order so images round-trip across platforms.
 */

static inline void po_store_le32(uint8_t* destination, uint32_t value)
{
	destination[0] = (uint8_t)value;
	destination[1] = (uint8_t)(value >> 8);
	destination[2] = (uint8_t)(value >> 16);
	destination[3] = (uint8_t)(value >> 24);
}

static inline void po_store_le64(uint8_t* destination, uint64_t value)
{
	unsigned int index;
	for (index = 0; index < 8; ++index) {
		destination[index] = (uint8_t)(value >> (index * 8));
	}
}

static inline uint32_t po_load_le32(const uint8_t* source)
{
	return (uint32_t)source[0] | ((uint32_t)source[1] << 8) | ((uint32_t)source[2] << 16) |
		   ((uint32_t)source[3] << 24);
}

static inline uint64_t po_load_le64(const uint8_t* source)
{
	uint64_t value = 0;
	unsigned int index;
	for (index = 0; index < 8; ++index) {
		value |= (uint64_t)source[index] << (index * 8);
	}
	return value;
}

#endif
