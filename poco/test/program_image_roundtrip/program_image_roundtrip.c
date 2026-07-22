#include "bytecode_container.h"
#include "program_image.h"

#include "poco_test_bytes.h"

#include <poco/poco.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check(int condition, const char* message)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "program image: %s\n", message);
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

int main(void)
{
	static const char source[] =
		"char text[] = \"abc\";\n"
		"int helper(int x) { return x + 40; }\n"
		"int main(void) { return helper(1) + fabs(-5.0); }\n";
	PocoVm* vm = NULL;
	PocoProgram* compiled = NULL;
	PocoProgram* decoded = NULL;
	uint8_t* image = NULL;
	size_t image_size = 0;
	uint8_t* container = NULL;
	size_t container_size = 0;
	PoBytecodeEnvelopeView view;
	uint8_t source_hash[POCO_BLAKE3_HASH_SIZE];
	int32_t reference = 0;
	int32_t result = 0;
	int ok = 1;
	uint64_t struct_offset;
	uint64_t struct_size;
	PoProgramImageStatus decode_status;
	uint8_t* noncanonical = NULL;
	PocoProgram* rejected = NULL;

	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "VM creation failed");
	ok &= check(poco_vm_register_standard_library(vm) == POCO_STATUS_OK,
				"standard library registration failed");
	{
		PocoStatus compile_status =
			poco_vm_compile_buffer(vm, "roundtrip.poc", source, sizeof(source) - 1, &compiled);
		if (compile_status != POCO_STATUS_OK) {
			fprintf(stderr, "program image: compile status %d: %s\n", (int)compile_status,
					poco_get_last_error(vm));
		}
		ok &= check(compile_status == POCO_STATUS_OK, "reference compilation failed");
	}
	if (compiled == NULL) {
		goto CLEANUP;
	}
	ok &= check(poco_vm_run(vm, compiled, NULL, &reference) == POCO_STATUS_OK,
				"reference run failed");
	ok &= check(reference == 46, "reference result mismatch");
	ok &= check(po_program_image_encode(compiled, POCO_DEBUG_LEVEL_MINIMAL, &image, &image_size) ==
					PO_PROGRAM_IMAGE_OK,
				"image encoding failed");
	ok &= check(image_size >= 160 && memcmp(image, "POIMG001", 8) == 0, "image header is missing");
	ok &= check(read_u32(image + 8) == 3 && read_u32(image + 12) == 6,
				"image header is not canonical multi-source version 3");
	ok &= check(read_u32(image + 40) == PO_PROGRAM_IMAGE_SECTION_CODE &&
					read_u32(image + 64) == PO_PROGRAM_IMAGE_SECTION_CONSTANTS &&
					read_u32(image + 88) == PO_PROGRAM_IMAGE_SECTION_PROTOTYPES &&
					read_u32(image + 112) == PO_PROGRAM_IMAGE_SECTION_SYMBOLS &&
					read_u32(image + 136) == PO_PROGRAM_IMAGE_SECTION_STRUCT_DEFINITIONS,
				"section table order changed");
	struct_offset = read_u64(image + 144);
	struct_size = read_u64(image + 152);
	ok &= check(struct_size == 4 && struct_offset + struct_size < image_size &&
					read_u32(image + struct_offset) == 0,
				"struct-definition placeholder is not reserved");
	noncanonical = malloc(image_size + 1);
	ok &= check(noncanonical != NULL, "non-canonical image allocation failed");
	if (noncanonical != NULL) {
		memcpy(noncanonical, image, image_size);
		noncanonical[image_size] = 0;
		ok &= check(
			po_program_image_decode(vm, noncanonical, image_size + 1, POCO_DEBUG_LEVEL_MINIMAL,
									NULL, &rejected) == PO_PROGRAM_IMAGE_MALFORMED,
			"image with trailing payload bytes was accepted");
		ok &= check(rejected == NULL, "rejected image returned a program");
	}

	po_blake3_hash(source, sizeof(source) - 1, source_hash);
	ok &= check(
		po_bytecode_envelope_write_hash(source_hash, image, image_size, PO_BYTECODE_DEBUG_MINIMAL,
										&container, &container_size) == PO_BYTECODE_CONTAINER_OK,
		"envelope write failed");
	ok &= check(!contains_bytes(container, container_size, source, sizeof(source) - 1),
				"program image envelope retained source bytes");
	free(image);
	image = NULL;
	poco_program_destroy(compiled);
	compiled = NULL;
	ok &= check(
		po_bytecode_envelope_read(container, container_size, &view) == PO_BYTECODE_CONTAINER_OK,
		"envelope validation failed");
	ok &= check(memcmp(view.source_hash, source_hash, sizeof(source_hash)) == 0,
				"program image envelope lost the source content address");
	decode_status = po_program_image_decode(vm, view.image, view.image_size,
											POCO_DEBUG_LEVEL_MINIMAL, NULL, &decoded);
	if (decode_status != PO_PROGRAM_IMAGE_OK) {
		fprintf(stderr, "program image: decode status %d\n", (int)decode_status);
	}
	ok &= check(decode_status == PO_PROGRAM_IMAGE_OK, "image decoding failed");
	ok &= check(poco_vm_run(vm, decoded, NULL, &result) == POCO_STATUS_OK, "decoded run failed");
	ok &= check(result == reference, "decoded result differs from compiled result");

CLEANUP:
	poco_program_destroy(decoded);
	poco_program_destroy(rejected);
	poco_program_destroy(compiled);
	poco_vm_destroy(vm);
	free(noncanonical);
	free(container);
	free(image);
	if (!ok) {
		return 1;
	}
	printf("program image round-trip passed\n");
	return 0;
}
