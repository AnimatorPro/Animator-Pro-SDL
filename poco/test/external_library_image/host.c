#include "bytecode_container.h"
#include "poco_endian.h"
#include "program_image.h"
#include "program_internal.h"
#include "pocoload.h"

#include "poco_test_bytes.h"

#include <poco/poco.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	ARCHIVE_HEADER_SIZE = 80,
	ARCHIVE_SOURCE_TABLE_SIZE_OFFSET = 32,
	ARCHIVE_IMAGE_SIZE_OFFSET = 40
};

static int check(int condition, const char* message)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "external library image: %s\n", message);
	return 0;
}

static int contains_bytes(const uint8_t* haystack, size_t haystack_size, const void* needle_data,
						  size_t needle_size)
{
	const uint8_t* needle = needle_data;
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
	static const char library_name[] = "serialize_fixture.poe";
	static const char source[] =
		"#pragma poco library \"serialize_fixture.poe\"\n"
		"int main(void) { return ExternalValue(); }\n";
	PocoVm* vm = NULL;
	PocoProgram* compiled = NULL;
	PocoProgram* decoded = NULL;
	PocoProgram* rejected = NULL;
	uint8_t* bytes = NULL;
	uint8_t* malformed_image = NULL;
	uint8_t* reserialized = NULL;
	size_t size = 0;
	size_t reserialized_size = 0;
	PoBytecodeEnvelopeView view;
	const uint8_t* image = NULL;
	size_t image_size = 0;
	size_t source_table_size;
	uint64_t library_offset;
	uint64_t library_size;
	int32_t result = 0;
	int ok = 1;

	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "VM creation failed");
	ok &= check(
		poco_vm_add_library_path(vm, POCO_EXTERNAL_LIBRARY_IMAGE_MODULE_DIR) == POCO_STATUS_OK,
		"module search path registration failed");
	ok &= check(poco_vm_compile_buffer(vm, "external-library-image.poc", source, sizeof(source) - 1,
									   &compiled) == POCO_STATUS_OK,
				"external-library compilation failed");
	if (compiled == NULL) {
		goto CLEANUP;
	}
	ok &= check(compiled->code.loaded_libraries != NULL,
				"compiled program did not retain its external-library list");
	ok &= check(poco_program_serialize_buffer(compiled, POCO_DEBUG_LEVEL_MINIMAL, NULL, 0, &size) ==
					POCO_STATUS_BUFFER_TOO_SMALL,
				"serialized size query failed");
	bytes = malloc(size);
	ok &= check(bytes != NULL, "serialized buffer allocation failed");
	if (bytes == NULL) {
		goto CLEANUP;
	}
	ok &= check(poco_program_serialize_buffer(compiled, POCO_DEBUG_LEVEL_MINIMAL, bytes, size,
											  &size) == POCO_STATUS_OK,
				"external-library serialization failed");
	ok &= check(po_bytecode_envelope_read(bytes, size, &view) == PO_BYTECODE_CONTAINER_OK,
				"serialized envelope could not be read");
	if (view.image == NULL || view.image_size < ARCHIVE_HEADER_SIZE) {
		goto CLEANUP;
	}
	source_table_size = (size_t)read_u64(view.image + ARCHIVE_SOURCE_TABLE_SIZE_OFFSET);
	image_size = (size_t)read_u64(view.image + ARCHIVE_IMAGE_SIZE_OFFSET);
	ok &= check(source_table_size <= view.image_size - ARCHIVE_HEADER_SIZE &&
					image_size == view.image_size - ARCHIVE_HEADER_SIZE - source_table_size,
				"serialized archive framing is invalid");
	image = view.image + ARCHIVE_HEADER_SIZE + source_table_size;
	ok &= check(read_u32(image + 8) == 3 && read_u32(image + 12) == 6,
				"external-library image did not use format version 3");
	ok &= check(read_u32(image + 160) == PO_PROGRAM_IMAGE_SECTION_EXTERNAL_LIBRARIES,
				"external-library section was not appended to the image");
	library_offset = read_u64(image + 168);
	library_size = read_u64(image + 176);
	ok &= check(library_offset + library_size == image_size && library_size >= 8 &&
					read_u32(image + library_offset) == 1 &&
					read_u32(image + library_offset + 4) == sizeof(library_name) - 1 &&
					memcmp(image + library_offset + 8, library_name, sizeof(library_name) - 1) == 0,
				"external-library section did not preserve the pragma name");
	ok &= check(contains_bytes(bytes, size, library_name, sizeof(library_name) - 1),
				"serialized bytes omitted the pragma name");
	ok &= check(!contains_bytes(bytes, size, POCO_EXTERNAL_LIBRARY_IMAGE_MODULE_DIR,
								sizeof(POCO_EXTERNAL_LIBRARY_IMAGE_MODULE_DIR) - 1),
				"serialized bytes retained the resolved module directory");
	ok &= check(!contains_bytes(bytes, size, source, sizeof(source) - 1),
				"serialized bytes retained source text");

	malformed_image = malloc(image_size);
	ok &= check(malformed_image != NULL, "malformed image allocation failed");
	if (malformed_image != NULL) {
		memcpy(malformed_image, image, image_size);
		po_store_le32(malformed_image + library_offset + 4, UINT32_MAX);
		ok &=
			check(po_program_image_decode(vm, malformed_image, image_size, POCO_DEBUG_LEVEL_MINIMAL,
										  NULL, &rejected) == PO_PROGRAM_IMAGE_MALFORMED &&
					  rejected == NULL,
				  "malformed external-library name length was accepted");
	}
	ok &= check(poco_vm_deserialize_buffer(vm, bytes, size, &decoded) == POCO_STATUS_OK,
				"external-library image decoding failed");
	ok &= check(decoded != NULL && decoded->code.loaded_libraries != NULL &&
					decoded->code.loaded_libraries->next == NULL &&
					strcmp(poco_loaded_library_requested_name(decoded->code.loaded_libraries),
						   library_name) == 0,
				"decoded program did not carry the pragma name list");
	ok &= check(poco_vm_run(vm, decoded, NULL, &result) == POCO_STATUS_OK && result == 1,
				"decoded program did not use its rebound external call");
	ok &= check(poco_vm_run(vm, decoded, NULL, &result) == POCO_STATUS_OK && result == 2,
				"external library init did not run again on the second run");
	ok &= check(poco_program_serialize_buffer(decoded, POCO_DEBUG_LEVEL_MINIMAL, NULL, 0,
											  &reserialized_size) == POCO_STATUS_BUFFER_TOO_SMALL &&
					reserialized_size == size,
				"decoded image size changed when reserialized");
	reserialized = malloc(reserialized_size);
	ok &= check(reserialized != NULL, "reserialized buffer allocation failed");
	if (reserialized != NULL) {
		ok &= check(poco_program_serialize_buffer(decoded, POCO_DEBUG_LEVEL_MINIMAL, reserialized,
												  reserialized_size,
												  &reserialized_size) == POCO_STATUS_OK &&
						memcmp(bytes, reserialized, size) == 0,
					"decoded external-library image was not byte-identical when reserialized");
	}

CLEANUP:
	poco_program_destroy(rejected);
	poco_program_destroy(decoded);
	poco_program_destroy(compiled);
	poco_vm_destroy(vm);
	free(reserialized);
	free(malformed_image);
	free(bytes);
	if (!ok) {
		return 1;
	}
	printf("external library image serialization passed\n");
	return 0;
}
