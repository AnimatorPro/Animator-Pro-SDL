#include <poco/poco.h>

#include "poco_hash.h"
#include "poco_test_bytes.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	ENVELOPE_HEADER_SIZE = 120,
	ENVELOPE_VERSION_OFFSET = 8,
	ENVELOPE_IMAGE_HASH_OFFSET = 56,
	ENVELOPE_ABI_ENDIAN_OFFSET = 88,
	ENVELOPE_ABI_POINTER_OFFSET = 89,
	ENVELOPE_ABI_INT_OFFSET = 90,
	ENVELOPE_ABI_LONG_OFFSET = 91,
	ENVELOPE_ABI_ALIGN_OFFSET = 93,
	ENVELOPE_ABI_OFFSET_REPR_OFFSET = 94,
	ARCHIVE_HEADER_SIZE = 80,
	ARCHIVE_VERSION_OFFSET = 8,
	ARCHIVE_FLAGS_OFFSET = 12,
	ARCHIVE_SOURCE_TABLE_SIZE_OFFSET = 32,
	ARCHIVE_IMAGE_SIZE_OFFSET = 40,
	ARCHIVE_FIRST_SOURCE_PATH_SIZE_OFFSET = 88,
	ARCHIVE_FLAG_SOURCELESS = 1,
	PROGRAM_IMAGE_VERSION_OFFSET = 8,
	PROGRAM_IMAGE_LAST_SECTION_SIZE_OFFSET = 176
};

static int check(int condition, const char* message)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "load validation: %s\n", message);
	return 0;
}

static void repair_envelope_image_hash(uint8_t* bytes, size_t size)
{
	po_blake3_hash(bytes + ENVELOPE_HEADER_SIZE, size - ENVELOPE_HEADER_SIZE,
				   bytes + ENVELOPE_IMAGE_HASH_OFFSET);
}

static int expect_rejection(PocoVm* vm, const uint8_t* bytes, size_t size, PocoStatus expected,
							const char* message)
{
	PocoProgram* rejected = NULL;
	PocoStatus status = poco_vm_deserialize_buffer(vm, bytes, size, &rejected);
	int ok = check(status == expected && rejected == NULL, message);
	poco_program_destroy(rejected);
	return ok;
}

int main(void)
{
	static const char source[] =
		"int helper(int value) { return value * 3; }\n"
		"int main(void) { return helper(14); }\n";
	PocoVm* vm = NULL;
	PocoProgram* compiled = NULL;
	PocoProgram* loaded = NULL;
	uint8_t* bytes = NULL;
	uint8_t* changed = NULL;
	size_t size = 0;
	size_t written = 0;
	size_t source_table_size;
	size_t program_image_offset;
	FILE* file = NULL;
	int32_t result = 0;
	int ok = 1;

	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "VM creation failed");
	ok &= check(poco_vm_compile_buffer(vm, "load-validation.poc", source, sizeof(source) - 1,
									   &compiled) == POCO_STATUS_OK,
				"compile failed");
	if (compiled == NULL) {
		goto cleanup;
	}
	ok &= check(poco_program_serialize_buffer(compiled, POCO_DEBUG_LEVEL_MINIMAL, NULL, 0, &size) ==
						POCO_STATUS_BUFFER_TOO_SMALL &&
					size > ENVELOPE_HEADER_SIZE + ARCHIVE_HEADER_SIZE,
				"serialized size query failed");
	bytes = (uint8_t*)malloc(size);
	changed = (uint8_t*)malloc(size);
	ok &= check(bytes != NULL && changed != NULL, "fixture allocation failed");
	if (bytes == NULL || changed == NULL) {
		goto cleanup;
	}
	ok &= check(poco_program_serialize_buffer(compiled, POCO_DEBUG_LEVEL_MINIMAL, bytes, size,
											  &written) == POCO_STATUS_OK &&
					written == size,
				"serialization failed");
	ok &= check(
		read_u32(bytes + ENVELOPE_HEADER_SIZE + ARCHIVE_FLAGS_OFFSET) == ARCHIVE_FLAG_SOURCELESS &&
			read_u64(bytes + ENVELOPE_HEADER_SIZE + ARCHIVE_FIRST_SOURCE_PATH_SIZE_OFFSET) == 0,
		"serialized fixture is not source-less");
	ok &= check(poco_vm_deserialize_buffer(vm, bytes, size, &loaded) == POCO_STATUS_OK,
				"valid image did not load");
	ok &= check(poco_vm_run(vm, loaded, NULL, &result) == POCO_STATUS_OK && result == 42,
				"valid loaded image did not run");
	poco_program_destroy(loaded);
	loaded = NULL;

	/* A container whose ABI descriptor advertises a different but well-formed
	 * target still loads and runs. The serialized image is host-independent
	 * (little-endian codec, canonical opcode stream, instruction-ordinal debug
	 * offsets, layout recomputed on load), and the descriptor is informational
	 * and not covered by the image hash, so no repair is required. This stands
	 * in for a genuinely cross-ABI build, which single-arch CI cannot produce. */
	memcpy(changed, bytes, size);
	changed[ENVELOPE_ABI_ENDIAN_OFFSET] = 1;  /* claims a big-endian producer */
	changed[ENVELOPE_ABI_POINTER_OFFSET] = 4; /* 32-bit pointers */
	changed[ENVELOPE_ABI_INT_OFFSET] = 2;
	changed[ENVELOPE_ABI_LONG_OFFSET] = 4;
	changed[ENVELOPE_ABI_ALIGN_OFFSET] = 16;
	ok &= check(poco_vm_deserialize_buffer(vm, changed, size, &loaded) == POCO_STATUS_OK,
				"foreign-ABI image did not load");
	ok &= check(
		loaded != NULL && poco_vm_run(vm, loaded, NULL, &result) == POCO_STATUS_OK && result == 42,
		"foreign-ABI image did not run");
	poco_program_destroy(loaded);
	loaded = NULL;

	/* A malformed ABI descriptor is rejected before the image is interpreted:
	 * an unknown offset representation, a zero fundamental size, or an
	 * out-of-range endianness each fail container validation. */
	memcpy(changed, bytes, size);
	changed[ENVELOPE_ABI_OFFSET_REPR_OFFSET] = 0; /* native offsets are no longer emitted */
	ok &= expect_rejection(vm, changed, size, POCO_STATUS_IMAGE_CORRUPT,
						   "native-offset ABI representation was not rejected");

	memcpy(changed, bytes, size);
	changed[ENVELOPE_ABI_POINTER_OFFSET] = 0;
	ok &= expect_rejection(vm, changed, size, POCO_STATUS_IMAGE_CORRUPT,
						   "zero pointer size in ABI was not rejected");

	memcpy(changed, bytes, size);
	changed[ENVELOPE_ABI_ENDIAN_OFFSET] = 2;
	ok &= expect_rejection(vm, changed, size, POCO_STATUS_IMAGE_CORRUPT,
						   "out-of-range ABI endianness was not rejected");

	ok &= expect_rejection(vm, NULL, 0, POCO_STATUS_IMAGE_TRUNCATED,
						   "empty image was not diagnosed as truncated");
	ok &= expect_rejection(vm, bytes, ENVELOPE_HEADER_SIZE - 1, POCO_STATUS_IMAGE_TRUNCATED,
						   "short header was not diagnosed as truncated");
	ok &= expect_rejection(vm, bytes, size - 1, POCO_STATUS_IMAGE_TRUNCATED,
						   "short payload was not diagnosed as truncated");

	memcpy(changed, bytes, size);
	changed[size - 1] ^= 0x80;
	ok &= expect_rejection(vm, changed, size, POCO_STATUS_IMAGE_CORRUPT,
						   "bit-flipped payload was not diagnosed as corrupt");

	memcpy(changed, bytes, size);
	write_u32(changed + ENVELOPE_VERSION_OFFSET, 3);
	ok &= expect_rejection(vm, changed, size, POCO_STATUS_IMAGE_VERSION_MISMATCH,
						   "envelope version bump was not diagnosed");

	memcpy(changed, bytes, size);
	write_u32(changed + ENVELOPE_HEADER_SIZE + ARCHIVE_VERSION_OFFSET, 3);
	repair_envelope_image_hash(changed, size);
	ok &= expect_rejection(vm, changed, size, POCO_STATUS_IMAGE_VERSION_MISMATCH,
						   "archive version bump was not diagnosed");

	memcpy(changed, bytes, size);
	write_u32(changed + ENVELOPE_HEADER_SIZE + ARCHIVE_VERSION_OFFSET, 1);
	repair_envelope_image_hash(changed, size);
	ok &= expect_rejection(vm, changed, size, POCO_STATUS_IMAGE_VERSION_MISMATCH,
						   "legacy single-source archive version was not rejected");

	source_table_size =
		(size_t)read_u64(bytes + ENVELOPE_HEADER_SIZE + ARCHIVE_SOURCE_TABLE_SIZE_OFFSET);
	program_image_offset = ENVELOPE_HEADER_SIZE + ARCHIVE_HEADER_SIZE + source_table_size;
	ok &= check(program_image_offset + PROGRAM_IMAGE_LAST_SECTION_SIZE_OFFSET + 8 <= size,
				"program image framing is invalid");
	if (program_image_offset + PROGRAM_IMAGE_LAST_SECTION_SIZE_OFFSET + 8 <= size) {
		memcpy(changed, bytes, size);
		write_u32(changed + program_image_offset + PROGRAM_IMAGE_VERSION_OFFSET, 4);
		repair_envelope_image_hash(changed, size);
		ok &= expect_rejection(vm, changed, size, POCO_STATUS_IMAGE_VERSION_MISMATCH,
							   "program image version bump was not diagnosed");

		memcpy(changed, bytes, size);
		write_u32(changed + program_image_offset + PROGRAM_IMAGE_VERSION_OFFSET, 2);
		repair_envelope_image_hash(changed, size);
		ok &= expect_rejection(vm, changed, size, POCO_STATUS_IMAGE_VERSION_MISMATCH,
							   "legacy program image version was not rejected");

		memcpy(changed, bytes, size);
		write_u64(
			changed + program_image_offset + PROGRAM_IMAGE_LAST_SECTION_SIZE_OFFSET,
			read_u64(changed + program_image_offset + PROGRAM_IMAGE_LAST_SECTION_SIZE_OFFSET) + 1);
		repair_envelope_image_hash(changed, size);
		ok &= expect_rejection(vm, changed, size, POCO_STATUS_IMAGE_TRUNCATED,
							   "nested section underflow was not diagnosed as truncated");
	}

	memcpy(changed, bytes, size);
	write_u64(changed + ENVELOPE_HEADER_SIZE + ARCHIVE_IMAGE_SIZE_OFFSET,
			  read_u64(changed + ENVELOPE_HEADER_SIZE + ARCHIVE_IMAGE_SIZE_OFFSET) + 1);
	repair_envelope_image_hash(changed, size);
	ok &= expect_rejection(vm, changed, size, POCO_STATUS_IMAGE_TRUNCATED,
						   "archive size underflow was not diagnosed as truncated");

	memcpy(changed, bytes, size);
	write_u32(changed + ENVELOPE_HEADER_SIZE + ARCHIVE_FLAGS_OFFSET, 0);
	repair_envelope_image_hash(changed, size);
	ok &= expect_rejection(vm, changed, size, POCO_STATUS_IMAGE_CORRUPT,
						   "legacy source-present archive shape was accepted");

	memcpy(changed, bytes, size);
	write_u64(changed + ENVELOPE_HEADER_SIZE + ARCHIVE_FIRST_SOURCE_PATH_SIZE_OFFSET, 1);
	repair_envelope_image_hash(changed, size);
	ok &= expect_rejection(vm, changed, size, POCO_STATUS_IMAGE_CORRUPT,
						   "minimal archive with a source path was accepted");

	memcpy(changed, bytes, size);
	write_u32(changed + ENVELOPE_HEADER_SIZE + ARCHIVE_FLAGS_OFFSET, 0x80000000u);
	repair_envelope_image_hash(changed, size);
	ok &= expect_rejection(vm, changed, size, POCO_STATUS_IMAGE_CORRUPT,
						   "unknown archive flags were not diagnosed as corrupt");

	file = tmpfile();
	ok &= check(file != NULL, "temporary file creation failed");
	if (file != NULL) {
		ok &= check(fwrite(bytes, 1, size - 1, file) == size - 1 && fflush(file) == 0 &&
						fseek(file, 0, SEEK_SET) == 0,
					"truncated file fixture setup failed");
		ok &= check(poco_vm_deserialize_file(vm, file, &loaded) == POCO_STATUS_IMAGE_TRUNCATED &&
						loaded == NULL,
					"truncated file was not diagnosed as truncated");
	}

cleanup:
	if (file != NULL) {
		fclose(file);
	}
	free(changed);
	free(bytes);
	poco_program_destroy(loaded);
	poco_program_destroy(compiled);
	poco_vm_destroy(vm);
	if (!ok) {
		return 1;
	}
	printf("load validation fixtures passed\n");
	return 0;
}
