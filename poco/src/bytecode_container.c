#include "bytecode_container.h"

#include "poco_endian.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum {
	PO_BYTECODE_OFFSET_MAGIC = 0,
	PO_BYTECODE_OFFSET_VERSION = 8,
	PO_BYTECODE_OFFSET_HEADER_SIZE = 12,
	PO_BYTECODE_OFFSET_SECTION_COUNT = 16,
	PO_BYTECODE_OFFSET_DEBUG_LEVEL = 20,
	PO_BYTECODE_OFFSET_SOURCE_HASH = 24,
	PO_BYTECODE_OFFSET_IMAGE_HASH = 56,
	PO_BYTECODE_OFFSET_ABI = 88,
	PO_BYTECODE_OFFSET_SECTION_TYPE = 96,
	PO_BYTECODE_OFFSET_SECTION_FLAGS = 100,
	PO_BYTECODE_OFFSET_SECTION_DATA = 104,
	PO_BYTECODE_OFFSET_SECTION_SIZE = 112,
	PO_BYTECODE_SECTION_COUNT = 1,
	PO_BYTECODE_SECTION_IMAGE = 1
};

/* Byte layout of the 8-byte ABI block at PO_BYTECODE_OFFSET_ABI. */
enum {
	PO_BYTECODE_ABI_ENDIAN = 0,
	PO_BYTECODE_ABI_POINTER = 1,
	PO_BYTECODE_ABI_INT = 2,
	PO_BYTECODE_ABI_LONG = 3,
	PO_BYTECODE_ABI_DOUBLE = 4,
	PO_BYTECODE_ABI_ALIGN = 5,
	PO_BYTECODE_ABI_OFFSET_REPR = 6,
	PO_BYTECODE_ABI_RESERVED = 7,
	PO_BYTECODE_ABI_SIZE = 8
};

struct po_align_probe {
	char c;
	union {
		void* p;
		double d;
		long l;
	} u;
};

static uint8_t po_host_endianness(void)
{
	const uint16_t probe = 1;
	return ((const uint8_t*)&probe)[0] == 1 ? 0 : 1;
}

/* Stamp the producing host's ABI into the 8-byte block at PO_BYTECODE_OFFSET_ABI. */
static void po_fill_host_abi(uint8_t* abi)
{
	abi[PO_BYTECODE_ABI_ENDIAN] = po_host_endianness();
	abi[PO_BYTECODE_ABI_POINTER] = (uint8_t)sizeof(void*);
	abi[PO_BYTECODE_ABI_INT] = (uint8_t)sizeof(int);
	abi[PO_BYTECODE_ABI_LONG] = (uint8_t)sizeof(long);
	abi[PO_BYTECODE_ABI_DOUBLE] = (uint8_t)sizeof(double);
	abi[PO_BYTECODE_ABI_ALIGN] = (uint8_t)offsetof(struct po_align_probe, u);
	abi[PO_BYTECODE_ABI_OFFSET_REPR] = (uint8_t)PO_BYTECODE_OFFSET_REPR_ORDINAL;
	abi[PO_BYTECODE_ABI_RESERVED] = 0;
}

static const uint8_t po_bytecode_magic[POCO_BYTECODE_MAGIC_SIZE] = {'P', 'O', 'C',  'O',
																	'B', 'C', 0x0d, 0x0a};

PoBytecodeContainerStatus po_bytecode_envelope_write_hash(
	const uint8_t source_hash[POCO_BLAKE3_HASH_SIZE], const void* image, size_t image_size,
	PoBytecodeDebugLevel debug_level, uint8_t** out_container, size_t* out_container_size)
{
	uint8_t* container;
	size_t container_size;

	if (source_hash == NULL || out_container == NULL || out_container_size == NULL ||
		(image == NULL && image_size != 0) ||
		(debug_level != PO_BYTECODE_DEBUG_MINIMAL && debug_level != PO_BYTECODE_DEBUG_EXTENDED)) {
		return PO_BYTECODE_CONTAINER_INVALID_ARGUMENT;
	}

	*out_container = NULL;
	*out_container_size = 0;
	if (image_size > SIZE_MAX - POCO_BYTECODE_HEADER_SIZE) {
		return PO_BYTECODE_CONTAINER_INVALID_ARGUMENT;
	}
	container_size = POCO_BYTECODE_HEADER_SIZE + image_size;
	container = (uint8_t*)calloc(container_size, 1);
	if (container == NULL) {
		return PO_BYTECODE_CONTAINER_OUT_OF_MEMORY;
	}

	memcpy(container + PO_BYTECODE_OFFSET_MAGIC, po_bytecode_magic, sizeof(po_bytecode_magic));
	po_store_le32(container + PO_BYTECODE_OFFSET_VERSION, POCO_BYTECODE_FORMAT_VERSION);
	po_store_le32(container + PO_BYTECODE_OFFSET_HEADER_SIZE, POCO_BYTECODE_HEADER_SIZE);
	po_store_le32(container + PO_BYTECODE_OFFSET_SECTION_COUNT, PO_BYTECODE_SECTION_COUNT);
	po_store_le32(container + PO_BYTECODE_OFFSET_DEBUG_LEVEL, (uint32_t)debug_level);
	memcpy(container + PO_BYTECODE_OFFSET_SOURCE_HASH, source_hash, POCO_BLAKE3_HASH_SIZE);
	po_blake3_hash(image, image_size, container + PO_BYTECODE_OFFSET_IMAGE_HASH);
	po_fill_host_abi(container + PO_BYTECODE_OFFSET_ABI);
	po_store_le32(container + PO_BYTECODE_OFFSET_SECTION_TYPE, PO_BYTECODE_SECTION_IMAGE);
	po_store_le64(container + PO_BYTECODE_OFFSET_SECTION_DATA, POCO_BYTECODE_HEADER_SIZE);
	po_store_le64(container + PO_BYTECODE_OFFSET_SECTION_SIZE, (uint64_t)image_size);
	if (image_size != 0) {
		memcpy(container + POCO_BYTECODE_HEADER_SIZE, image, image_size);
	}

	*out_container = container;
	*out_container_size = container_size;
	return PO_BYTECODE_CONTAINER_OK;
}

PoBytecodeContainerStatus po_bytecode_envelope_write(const void* source, size_t source_size,
													 const void* image, size_t image_size,
													 PoBytecodeDebugLevel debug_level,
													 uint8_t** out_container,
													 size_t* out_container_size)
{
	uint8_t source_hash[POCO_BLAKE3_HASH_SIZE];

	if (source == NULL && source_size != 0) {
		return PO_BYTECODE_CONTAINER_INVALID_ARGUMENT;
	}
	po_blake3_hash(source, source_size, source_hash);
	return po_bytecode_envelope_write_hash(source_hash, image, image_size, debug_level,
										   out_container, out_container_size);
}

PoBytecodeContainerStatus po_bytecode_envelope_read(const void* container_data,
													size_t container_size,
													PoBytecodeEnvelopeView* out_view)
{
	const uint8_t* container = (const uint8_t*)container_data;
	uint64_t image_offset;
	uint64_t image_size;
	uint32_t debug_level;
	uint8_t image_hash[POCO_BLAKE3_HASH_SIZE];

	if (out_view == NULL || (container == NULL && container_size != 0)) {
		return PO_BYTECODE_CONTAINER_INVALID_ARGUMENT;
	}
	memset(out_view, 0, sizeof(*out_view));
	if (container_size < POCO_BYTECODE_HEADER_SIZE) {
		return PO_BYTECODE_CONTAINER_TRUNCATED;
	}
	if (memcmp(container + PO_BYTECODE_OFFSET_MAGIC, po_bytecode_magic,
			   sizeof(po_bytecode_magic)) != 0) {
		return PO_BYTECODE_CONTAINER_BAD_MAGIC;
	}
	if (po_load_le32(container + PO_BYTECODE_OFFSET_VERSION) != POCO_BYTECODE_FORMAT_VERSION) {
		return PO_BYTECODE_CONTAINER_VERSION_MISMATCH;
	}
	debug_level = po_load_le32(container + PO_BYTECODE_OFFSET_DEBUG_LEVEL);
	if (po_load_le32(container + PO_BYTECODE_OFFSET_HEADER_SIZE) != POCO_BYTECODE_HEADER_SIZE ||
		po_load_le32(container + PO_BYTECODE_OFFSET_SECTION_COUNT) != PO_BYTECODE_SECTION_COUNT ||
		(debug_level != PO_BYTECODE_DEBUG_MINIMAL && debug_level != PO_BYTECODE_DEBUG_EXTENDED) ||
		po_load_le32(container + PO_BYTECODE_OFFSET_SECTION_TYPE) != PO_BYTECODE_SECTION_IMAGE ||
		po_load_le32(container + PO_BYTECODE_OFFSET_SECTION_FLAGS) != 0) {
		return PO_BYTECODE_CONTAINER_BAD_LAYOUT;
	}
	{
		const uint8_t* abi = container + PO_BYTECODE_OFFSET_ABI;
		if (abi[PO_BYTECODE_ABI_ENDIAN] > 1 || abi[PO_BYTECODE_ABI_POINTER] == 0 ||
			abi[PO_BYTECODE_ABI_INT] == 0 || abi[PO_BYTECODE_ABI_LONG] == 0 ||
			abi[PO_BYTECODE_ABI_DOUBLE] == 0 || abi[PO_BYTECODE_ABI_ALIGN] == 0 ||
			abi[PO_BYTECODE_ABI_OFFSET_REPR] != PO_BYTECODE_OFFSET_REPR_ORDINAL) {
			return PO_BYTECODE_CONTAINER_BAD_LAYOUT;
		}
	}

	image_offset = po_load_le64(container + PO_BYTECODE_OFFSET_SECTION_DATA);
	image_size = po_load_le64(container + PO_BYTECODE_OFFSET_SECTION_SIZE);
	if (image_offset != POCO_BYTECODE_HEADER_SIZE) {
		return PO_BYTECODE_CONTAINER_BAD_LAYOUT;
	}
	if (image_size > (uint64_t)(container_size - POCO_BYTECODE_HEADER_SIZE)) {
		return PO_BYTECODE_CONTAINER_TRUNCATED;
	}
	if (image_size != (uint64_t)(container_size - POCO_BYTECODE_HEADER_SIZE)) {
		return PO_BYTECODE_CONTAINER_BAD_LAYOUT;
	}

	po_blake3_hash(container + (size_t)image_offset, (size_t)image_size, image_hash);
	if (!poco_hash_equal(image_hash, container + PO_BYTECODE_OFFSET_IMAGE_HASH)) {
		return PO_BYTECODE_CONTAINER_INTEGRITY_MISMATCH;
	}

	out_view->image = container + (size_t)image_offset;
	out_view->image_size = (size_t)image_size;
	out_view->debug_level = (PoBytecodeDebugLevel)debug_level;
	memcpy(out_view->source_hash, container + PO_BYTECODE_OFFSET_SOURCE_HASH,
		   POCO_BLAKE3_HASH_SIZE);
	out_view->abi.endianness = container[PO_BYTECODE_OFFSET_ABI + PO_BYTECODE_ABI_ENDIAN];
	out_view->abi.pointer_size = container[PO_BYTECODE_OFFSET_ABI + PO_BYTECODE_ABI_POINTER];
	out_view->abi.int_size = container[PO_BYTECODE_OFFSET_ABI + PO_BYTECODE_ABI_INT];
	out_view->abi.long_size = container[PO_BYTECODE_OFFSET_ABI + PO_BYTECODE_ABI_LONG];
	out_view->abi.double_size = container[PO_BYTECODE_OFFSET_ABI + PO_BYTECODE_ABI_DOUBLE];
	out_view->abi.max_align = container[PO_BYTECODE_OFFSET_ABI + PO_BYTECODE_ABI_ALIGN];
	out_view->abi.offset_repr = container[PO_BYTECODE_OFFSET_ABI + PO_BYTECODE_ABI_OFFSET_REPR];
	return PO_BYTECODE_CONTAINER_OK;
}

PoBytecodeContainerStatus po_bytecode_envelope_verify_source(const PoBytecodeEnvelopeView* view,
															 const void* source, size_t source_size)
{
	uint8_t source_hash[POCO_BLAKE3_HASH_SIZE];

	if (view == NULL || (source == NULL && source_size != 0)) {
		return PO_BYTECODE_CONTAINER_INVALID_ARGUMENT;
	}
	po_blake3_hash(source, source_size, source_hash);
	if (!poco_hash_equal(source_hash, view->source_hash)) {
		return PO_BYTECODE_CONTAINER_SOURCE_MISMATCH;
	}
	return PO_BYTECODE_CONTAINER_OK;
}
