#include "bytecode_container.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	OFFSET_VERSION = 8,
	OFFSET_HEADER_SIZE = 12,
	OFFSET_SECTION_COUNT = 16,
	OFFSET_DEBUG_LEVEL = 20,
	OFFSET_SOURCE_HASH = 24,
	OFFSET_IMAGE_HASH = 56,
	OFFSET_ABI = 88,
	OFFSET_SECTION_TYPE = 96,
	OFFSET_SECTION_FLAGS = 100,
	OFFSET_SECTION_DATA = 104,
	OFFSET_SECTION_SIZE = 112,
	OFFSET_ABI_POINTER = OFFSET_ABI + 1,
	OFFSET_ABI_OFFSET_REPR = OFFSET_ABI + 6
};

static const uint8_t empty_blake3[POCO_BLAKE3_HASH_SIZE] = {
	0xaf, 0x13, 0x49, 0xb9, 0xf5, 0xf9, 0xa1, 0xa6, 0xa0, 0x40, 0x4d, 0xea, 0x36, 0xdc, 0xc9, 0x49,
	0x9b, 0xcb, 0x25, 0xc9, 0xad, 0xc1, 0x12, 0xb7, 0xcc, 0x9a, 0x93, 0xca, 0xe4, 0x1f, 0x32, 0x62};

static int expect_bytes(const char* name, const uint8_t* actual, const uint8_t* expected,
						size_t size)
{
	if (memcmp(actual, expected, size) == 0) {
		return 1;
	}
	fprintf(stderr, "%s: canonical bytes differ\n", name);
	return 0;
}

static int test_round_trip(void)
{
	static const char source[] = "int main(void) { return 42; }";
	static const uint8_t image[] = {0x00, 0x01, 0x7f, 0x80, 0xfe, 0xff, 0x42};
	static const uint8_t magic[] = {'P', 'O', 'C', 'O', 'B', 'C', 0x0d, 0x0a};
	static const uint8_t version[] = {2, 0, 0, 0};
	static const uint8_t header_size[] = {120, 0, 0, 0};
	static const uint8_t section_count[] = {1, 0, 0, 0};
	static const uint8_t zero32[] = {0, 0, 0, 0};
	static const uint8_t section_offset[] = {120, 0, 0, 0, 0, 0, 0, 0};
	static const uint8_t section_size[] = {7, 0, 0, 0, 0, 0, 0, 0};
	uint8_t source_hash[POCO_BLAKE3_HASH_SIZE];
	uint8_t image_hash[POCO_BLAKE3_HASH_SIZE];
	uint8_t* container = NULL;
	size_t container_size = 0;
	PoBytecodeEnvelopeView view;
	int ok = 1;

	if (po_bytecode_envelope_write(source, sizeof(source) - 1, image, sizeof(image),
								   PO_BYTECODE_DEBUG_MINIMAL, &container,
								   &container_size) != PO_BYTECODE_CONTAINER_OK) {
		fprintf(stderr, "round-trip: write failed\n");
		return 0;
	}
	po_blake3_hash(source, sizeof(source) - 1, source_hash);
	po_blake3_hash(image, sizeof(image), image_hash);

	ok &= container_size == POCO_BYTECODE_HEADER_SIZE + sizeof(image);
	ok &= expect_bytes("magic", container, magic, sizeof(magic));
	ok &= expect_bytes("version", container + OFFSET_VERSION, version, sizeof(version));
	ok &= expect_bytes("header size", container + OFFSET_HEADER_SIZE, header_size,
					   sizeof(header_size));
	ok &= expect_bytes("section count", container + OFFSET_SECTION_COUNT, section_count,
					   sizeof(section_count));
	ok &= expect_bytes("debug level", container + OFFSET_DEBUG_LEVEL, zero32, sizeof(zero32));
	ok &= expect_bytes("source hash", container + OFFSET_SOURCE_HASH, source_hash,
					   sizeof(source_hash));
	ok &= expect_bytes("image hash", container + OFFSET_IMAGE_HASH, image_hash, sizeof(image_hash));
	ok &= expect_bytes("section type", container + OFFSET_SECTION_TYPE, section_count,
					   sizeof(section_count));
	ok &= expect_bytes("section flags", container + OFFSET_SECTION_FLAGS, zero32, sizeof(zero32));
	ok &= expect_bytes("section offset", container + OFFSET_SECTION_DATA, section_offset,
					   sizeof(section_offset));
	ok &= expect_bytes("section size", container + OFFSET_SECTION_SIZE, section_size,
					   sizeof(section_size));
	ok &= container[OFFSET_ABI_POINTER] == (uint8_t)sizeof(void*);
	ok &= container[OFFSET_ABI_OFFSET_REPR] == (uint8_t)PO_BYTECODE_OFFSET_REPR_ORDINAL;
	ok &= expect_bytes("image", container + POCO_BYTECODE_HEADER_SIZE, image, sizeof(image));

	if (po_bytecode_envelope_read(container, container_size, &view) != PO_BYTECODE_CONTAINER_OK) {
		fprintf(stderr, "round-trip: read failed\n");
		ok = 0;
	} else {
		ok &= view.image_size == sizeof(image);
		ok &= view.debug_level == PO_BYTECODE_DEBUG_MINIMAL;
		ok &= view.abi.offset_repr == PO_BYTECODE_OFFSET_REPR_ORDINAL;
		ok &= view.abi.pointer_size == (uint8_t)sizeof(void*);
		ok &= view.abi.double_size == (uint8_t)sizeof(double);
		ok &= expect_bytes("view image", view.image, image, sizeof(image));
		ok &= po_bytecode_envelope_verify_source(&view, source, sizeof(source) - 1) ==
			  PO_BYTECODE_CONTAINER_OK;
		ok &= po_bytecode_envelope_verify_source(&view, "changed", 7) ==
			  PO_BYTECODE_CONTAINER_SOURCE_MISMATCH;
	}
	free(container);
	return ok;
}

static int test_rejections(void)
{
	static const uint8_t image[] = {1, 2, 3, 4};
	uint8_t* container = NULL;
	size_t container_size = 0;
	PoBytecodeEnvelopeView view;
	int ok = 1;

	if (po_bytecode_envelope_write(NULL, 0, image, sizeof(image), PO_BYTECODE_DEBUG_EXTENDED,
								   &container, &container_size) != PO_BYTECODE_CONTAINER_OK) {
		return 0;
	}

	container[OFFSET_VERSION] = 3;
	ok &= po_bytecode_envelope_read(container, container_size, &view) ==
		  PO_BYTECODE_CONTAINER_VERSION_MISMATCH;
	container[OFFSET_VERSION] = POCO_BYTECODE_FORMAT_VERSION;
	ok &= po_bytecode_envelope_read(container, container_size, &view) == PO_BYTECODE_CONTAINER_OK &&
		  view.debug_level == PO_BYTECODE_DEBUG_EXTENDED;
	container[OFFSET_DEBUG_LEVEL] = 2;
	ok &= po_bytecode_envelope_read(container, container_size, &view) ==
		  PO_BYTECODE_CONTAINER_BAD_LAYOUT;
	container[OFFSET_DEBUG_LEVEL] = PO_BYTECODE_DEBUG_EXTENDED;
	container[POCO_BYTECODE_HEADER_SIZE + 1] ^= 0x80;
	ok &= po_bytecode_envelope_read(container, container_size, &view) ==
		  PO_BYTECODE_CONTAINER_INTEGRITY_MISMATCH;
	container[POCO_BYTECODE_HEADER_SIZE + 1] ^= 0x80;
	ok &= po_bytecode_envelope_read(container, container_size - 1, &view) ==
		  PO_BYTECODE_CONTAINER_TRUNCATED;

	free(container);
	return ok;
}

static int test_empty_payload(void)
{
	uint8_t* container = NULL;
	size_t container_size = 0;
	PoBytecodeEnvelopeView view;
	int ok;

	if (po_bytecode_envelope_write(NULL, 0, NULL, 0, PO_BYTECODE_DEBUG_MINIMAL, &container,
								   &container_size) != PO_BYTECODE_CONTAINER_OK) {
		return 0;
	}
	ok = container_size == POCO_BYTECODE_HEADER_SIZE &&
		 expect_bytes("empty source hash", container + OFFSET_SOURCE_HASH, empty_blake3,
					  sizeof(empty_blake3)) &&
		 expect_bytes("empty image hash", container + OFFSET_IMAGE_HASH, empty_blake3,
					  sizeof(empty_blake3)) &&
		 po_bytecode_envelope_read(container, container_size, &view) == PO_BYTECODE_CONTAINER_OK &&
		 view.image_size == 0;
	free(container);
	return ok;
}

int main(void)
{
	if (!test_round_trip() || !test_rejections() || !test_empty_payload()) {
		return 1;
	}
	printf("bytecode envelope fixtures passed\n");
	return 0;
}
