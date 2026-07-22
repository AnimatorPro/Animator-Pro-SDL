#include <poco/poco.h>

#include "poco_hash.h"
#include "poco_test_bytes.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	ENVELOPE_HEADER_SIZE = 120,
	ENVELOPE_IMAGE_HASH_OFFSET = 56,
	ARCHIVE_FLAGS_OFFSET = 12,
	ARCHIVE_FIRST_SOURCE_PATH_SIZE_OFFSET = 88,
	ARCHIVE_SOURCE_HASH_OFFSET = 48,
	ARCHIVE_FLAG_SOURCELESS = 1
};

static int check(int condition, const char* message)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "serialization API: %s\n", message);
	return 0;
}

static int run_result(PocoVm* vm, PocoProgram* program, int32_t expected, const char* message)
{
	int32_t result = 0;
	return check(poco_vm_run(vm, program, NULL, &result) == POCO_STATUS_OK && result == expected,
				 message);
}

static int contains_bytes(const uint8_t* haystack, size_t haystack_size, const void* needle_data,
						  size_t needle_size)
{
	const uint8_t* needle = (const uint8_t*)needle_data;
	size_t offset;

	if (needle_size == 0 || needle_size > haystack_size) {
		return 0;
	}
	for (offset = 0; offset <= haystack_size - needle_size; ++offset) {
		if (memcmp(haystack + offset, needle, needle_size) == 0) {
			return 1;
		}
	}
	return 0;
}

int main(void)
{
	static const char source[] =
		"int helper(int value) { return value * 3; }\n"
		"int main(void) { return helper(14); }\n";
	PocoVm* vm = NULL;
	PocoProgram* compiled = NULL;
	PocoProgram* memory_program = NULL;
	PocoProgram* file_program = NULL;
	PocoProgram* empty_name_compiled = NULL;
	PocoProgram* empty_name_program = NULL;
	uint8_t* bytes = NULL;
	uint8_t* changed = NULL;
	uint8_t* empty_name_bytes = NULL;
	size_t required = 0;
	size_t repeated = 0;
	size_t empty_name_size = 0;
	FILE* file = NULL;
	int ok = 1;

	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "VM creation failed");
	ok &= check(poco_vm_compile_buffer(vm, "serialization-api.poc", source, sizeof(source) - 1,
									   &compiled) == POCO_STATUS_OK,
				"compile failed");
	if (compiled == NULL) {
		goto cleanup;
	}
	ok &= run_result(vm, compiled, 42, "freshly compiled result mismatch");
	ok &= check(poco_program_serialize_buffer(compiled, POCO_DEBUG_LEVEL_MINIMAL, NULL, 0,
											  &required) == POCO_STATUS_BUFFER_TOO_SMALL &&
					required > 0,
				"memory size query failed");
	bytes = (uint8_t*)malloc(required);
	ok &= check(bytes != NULL, "memory image allocation failed");
	if (bytes == NULL) {
		goto cleanup;
	}
	repeated = required;
	ok &=
		check(poco_program_serialize_buffer(compiled, POCO_DEBUG_LEVEL_MINIMAL, bytes, required - 1,
											&repeated) == POCO_STATUS_BUFFER_TOO_SMALL &&
				  repeated == required,
			  "undersized caller buffer was not reported");
	ok &= check(poco_program_serialize_buffer(compiled, POCO_DEBUG_LEVEL_MINIMAL, bytes, required,
											  &repeated) == POCO_STATUS_OK &&
					repeated == required,
				"memory serialization failed");
	ok &= check(
		read_u32(bytes + ENVELOPE_HEADER_SIZE + ARCHIVE_FLAGS_OFFSET) == ARCHIVE_FLAG_SOURCELESS,
		"source-less archive flag is missing");
	ok &= check(read_u64(bytes + ENVELOPE_HEADER_SIZE + ARCHIVE_FIRST_SOURCE_PATH_SIZE_OFFSET) == 0,
				"minimal serialized image retained a source path");
	ok &= check(!contains_bytes(bytes, required, source, sizeof(source) - 1),
				"serialized image contains source bytes");
	ok &= check(poco_vm_deserialize_buffer(vm, bytes, required, &memory_program) == POCO_STATUS_OK,
				"memory deserialization failed");
	ok &= run_result(vm, memory_program, 42, "memory round-trip result mismatch");

	changed = (uint8_t*)malloc(required);
	ok &= check(changed != NULL, "hash fixture allocation failed");
	if (changed != NULL) {
		PocoProgram* rejected = NULL;

		memcpy(changed, bytes, required);
		changed[required - 1] ^= 1;
		ok &= check(poco_vm_deserialize_buffer(vm, changed, required, &rejected) ==
							POCO_STATUS_IMAGE_CORRUPT &&
						rejected == NULL,
					"image-integrity mismatch was accepted");

		memcpy(changed, bytes, required);
		changed[ENVELOPE_HEADER_SIZE + ARCHIVE_SOURCE_HASH_OFFSET] ^= 1;
		/* Repair the envelope image digest only. The archive copy must still
		 * match the retained source-content address in the envelope. */
		po_blake3_hash(changed + ENVELOPE_HEADER_SIZE, required - ENVELOPE_HEADER_SIZE,
					   changed + ENVELOPE_IMAGE_HASH_OFFSET);
		ok &= check(poco_vm_deserialize_buffer(vm, changed, required, &rejected) ==
							POCO_STATUS_IMAGE_CORRUPT &&
						rejected == NULL,
					"source-content address mismatch was accepted");
	}

	file = tmpfile();
	ok &= check(file != NULL, "temporary file creation failed");
	if (file == NULL) {
		goto cleanup;
	}
	ok &= check(
		poco_program_serialize_file(compiled, POCO_DEBUG_LEVEL_MINIMAL, file) == POCO_STATUS_OK,
		"file serialization failed");
	ok &= check(fflush(file) == 0 && fseek(file, 0, SEEK_SET) == 0, "temporary file rewind failed");
	ok &= check(poco_vm_deserialize_file(vm, file, &file_program) == POCO_STATUS_OK,
				"file deserialization failed");
	ok &= run_result(vm, file_program, 42, "file round-trip result mismatch");

	/* Re-serialization proves decoded programs retained source name and hash. */
	repeated = 0;
	ok &= check(poco_program_serialize_buffer(memory_program, POCO_DEBUG_LEVEL_MINIMAL, NULL, 0,
											  &repeated) == POCO_STATUS_BUFFER_TOO_SMALL &&
					repeated == required,
				"decoded program did not retain source metadata");

	ok &= check(poco_vm_compile_buffer(vm, "", source, sizeof(source) - 1, &empty_name_compiled) ==
					POCO_STATUS_OK,
				"empty diagnostic source name did not compile");
	ok &=
		check(poco_program_serialize_buffer(empty_name_compiled, POCO_DEBUG_LEVEL_MINIMAL, NULL, 0,
											&empty_name_size) == POCO_STATUS_BUFFER_TOO_SMALL &&
				  empty_name_size > 0,
			  "empty-name image size query failed");
	empty_name_bytes = (uint8_t*)malloc(empty_name_size);
	ok &= check(empty_name_bytes != NULL, "empty-name image allocation failed");
	if (empty_name_bytes != NULL) {
		ok &= check(poco_program_serialize_buffer(empty_name_compiled, POCO_DEBUG_LEVEL_MINIMAL,
												  empty_name_bytes, empty_name_size,
												  &repeated) == POCO_STATUS_OK,
					"empty-name memory serialization failed");
		ok &= check(poco_vm_deserialize_buffer(vm, empty_name_bytes, empty_name_size,
											   &empty_name_program) == POCO_STATUS_OK,
					"empty-name memory deserialization failed");
		ok &= run_result(vm, empty_name_program, 42, "empty-name round-trip result mismatch");
	}

cleanup:
	if (file != NULL) {
		fclose(file);
	}
	free(bytes);
	free(changed);
	free(empty_name_bytes);
	poco_program_destroy(empty_name_program);
	poco_program_destroy(empty_name_compiled);
	poco_program_destroy(file_program);
	poco_program_destroy(memory_program);
	poco_program_destroy(compiled);
	poco_vm_destroy(vm);
	if (!ok) {
		return 1;
	}
	printf("serialization API round-trips passed\n");
	return 0;
}
