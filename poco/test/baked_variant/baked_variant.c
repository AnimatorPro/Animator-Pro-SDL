#include <poco/poco.h>

#include "poco_test_bytes.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	ENVELOPE_SOURCE_HASH_OFFSET = 24,
	ENVELOPE_SOURCE_HASH_SIZE = 32,
	ENVELOPE_HEADER_SIZE = 120,
	ARCHIVE_FLAGS_OFFSET = 12,
	ARCHIVE_FIRST_SOURCE_PATH_SIZE_OFFSET = 88,
	ARCHIVE_FLAG_SOURCELESS = 1
};

static int check(int condition, const char* message)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "source-less serialization: %s\n", message);
	return 0;
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

static int run_result(PocoVm* vm, PocoProgram* program, int32_t expected, const char* message)
{
	int32_t result = 0;
	return check(poco_vm_run(vm, program, NULL, &result) == POCO_STATUS_OK && result == expected,
				 message);
}

int main(void)
{
	static const char source[] =
		"int source_less_helper(int value) { return value * 7; }\n"
		"int main(void) { return source_less_helper(6); }\n";
	PocoVm* vm = NULL;
	PocoProgram* compiled = NULL;
	PocoProgram* loaded = NULL;
	PocoProgram* file_loaded = NULL;
	uint8_t* bytes = NULL;
	uint8_t* serialized_again = NULL;
	size_t size = 0;
	size_t serialized_again_size = 0;
	size_t written = 0;
	FILE* file = NULL;
	int ok = 1;

	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "VM creation failed");
	ok &= check(poco_vm_compile_buffer(vm, "source-less.poc", source, sizeof(source) - 1,
									   &compiled) == POCO_STATUS_OK,
				"compile failed");
	if (compiled == NULL) {
		goto cleanup;
	}
	ok &= run_result(vm, compiled, 42, "compiled result mismatch");
	ok &= check(poco_program_serialize_buffer(compiled, POCO_DEBUG_LEVEL_MINIMAL, NULL, 0, &size) ==
						POCO_STATUS_BUFFER_TOO_SMALL &&
					size > 0,
				"size query failed");
	bytes = (uint8_t*)malloc(size);
	ok &= check(bytes != NULL, "image allocation failed");
	if (bytes == NULL) {
		goto cleanup;
	}
	ok &= check(poco_program_serialize_buffer(compiled, POCO_DEBUG_LEVEL_MINIMAL, bytes, size,
											  &written) == POCO_STATUS_OK &&
					written == size,
				"serialization failed");
	ok &= check(
		read_u32(bytes + ENVELOPE_HEADER_SIZE + ARCHIVE_FLAGS_OFFSET) == ARCHIVE_FLAG_SOURCELESS,
		"source-less flag was not encoded");
	ok &= check(read_u64(bytes + ENVELOPE_HEADER_SIZE + ARCHIVE_FIRST_SOURCE_PATH_SIZE_OFFSET) == 0,
				"minimal image retained a source path");
	ok &= check(!contains_bytes(bytes, size, source, sizeof(source) - 1),
				"source text appears in image");

	ok &= check(poco_vm_deserialize_buffer(vm, bytes, size, &loaded) == POCO_STATUS_OK,
				"deserialization failed");
	ok &= run_result(vm, loaded, 42, "loaded result mismatch");
	ok &= check(
		poco_program_serialize_buffer(loaded, POCO_DEBUG_LEVEL_MINIMAL, NULL, 0,
									  &serialized_again_size) == POCO_STATUS_BUFFER_TOO_SMALL &&
			serialized_again_size == size,
		"loaded program did not retain serialization metadata");
	serialized_again = (uint8_t*)malloc(serialized_again_size);
	ok &= check(serialized_again != NULL, "repeat image allocation failed");
	if (serialized_again != NULL) {
		ok &=
			check(poco_program_serialize_buffer(loaded, POCO_DEBUG_LEVEL_MINIMAL, serialized_again,
												serialized_again_size, &written) == POCO_STATUS_OK,
				  "loaded program could not be serialized again");
		ok &= check(memcmp(serialized_again + ENVELOPE_SOURCE_HASH_OFFSET,
						   bytes + ENVELOPE_SOURCE_HASH_OFFSET, ENVELOPE_SOURCE_HASH_SIZE) == 0,
					"repeat serialization changed the source content address");
		ok &= check(
			!contains_bytes(serialized_again, serialized_again_size, source, sizeof(source) - 1),
			"repeat serialization contains source bytes");
	}

	file = tmpfile();
	ok &= check(file != NULL, "temporary file creation failed");
	if (file != NULL) {
		ok &= check(
			poco_program_serialize_file(compiled, POCO_DEBUG_LEVEL_MINIMAL, file) == POCO_STATUS_OK,
			"file serialization failed");
		ok &= check(fflush(file) == 0 && fseek(file, 0, SEEK_SET) == 0, "file rewind failed");
		ok &= check(poco_vm_deserialize_file(vm, file, &file_loaded) == POCO_STATUS_OK,
					"file deserialization failed");
		ok &= run_result(vm, file_loaded, 42, "file-loaded result mismatch");
	}

cleanup:
	if (file != NULL) {
		fclose(file);
	}
	free(serialized_again);
	free(bytes);
	poco_program_destroy(file_loaded);
	poco_program_destroy(loaded);
	poco_program_destroy(compiled);
	poco_vm_destroy(vm);
	if (!ok) {
		return 1;
	}
	printf("source-less serialization invariant passed\n");
	return 0;
}
