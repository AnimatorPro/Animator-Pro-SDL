#include <poco/poco.h>

#include "bytecode_container.h"
#include "poco_endian.h"
#include "poco_hash.h"
#include "program_image.h"
#include "program_internal.h"

#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	ARCHIVE_HEADER_SIZE = 80,
	ARCHIVE_SOURCE_TABLE_SIZE_OFFSET = 32,
	ARCHIVE_IMAGE_SIZE_OFFSET = 40,
	ARCHIVE_SOURCE_HASH_OFFSET = 48,
	SOURCE_ENTRY_SIZE = 48
};

typedef struct SerializedProgram {
	uint8_t* bytes;
	size_t size;
	PoBytecodeEnvelopeView view;
	const uint8_t* source_name;
	size_t source_name_size;
	const uint8_t* source_path;
	size_t source_path_size;
	const uint8_t* image;
	size_t image_size;
} SerializedProgram;

static int check(int condition, const char* message)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "debug metadata: %s\n", message);
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

static int serialize_program(const PocoProgram* program, PocoDebugLevel debug_level,
							 SerializedProgram* serialized)
{
	const uint8_t* archive;
	size_t archive_size;
	size_t data_size;

	memset(serialized, 0, sizeof(*serialized));
	if (poco_program_serialize_buffer(program, debug_level, NULL, 0, &serialized->size) !=
			POCO_STATUS_BUFFER_TOO_SMALL ||
		serialized->size == 0) {
		return 0;
	}
	serialized->bytes = (uint8_t*)malloc(serialized->size);
	if (serialized->bytes == NULL ||
		poco_program_serialize_buffer(program, debug_level, serialized->bytes, serialized->size,
									  &serialized->size) != POCO_STATUS_OK ||
		po_bytecode_envelope_read(serialized->bytes, serialized->size, &serialized->view) !=
			PO_BYTECODE_CONTAINER_OK) {
		return 0;
	}
	archive = serialized->view.image;
	archive_size = serialized->view.image_size;
	if (archive_size < ARCHIVE_HEADER_SIZE) {
		return 0;
	}
	data_size = (size_t)po_load_le64(archive + ARCHIVE_SOURCE_TABLE_SIZE_OFFSET);
	serialized->source_name_size = (size_t)po_load_le64(archive + ARCHIVE_HEADER_SIZE);
	serialized->source_path_size = (size_t)po_load_le64(archive + ARCHIVE_HEADER_SIZE + 8);
	serialized->image_size = (size_t)po_load_le64(archive + ARCHIVE_IMAGE_SIZE_OFFSET);
	if (data_size < SOURCE_ENTRY_SIZE ||
		serialized->source_name_size > data_size - SOURCE_ENTRY_SIZE ||
		serialized->source_path_size >
			data_size - SOURCE_ENTRY_SIZE - serialized->source_name_size ||
		serialized->image_size != archive_size - ARCHIVE_HEADER_SIZE - data_size) {
		return 0;
	}
	serialized->source_name = archive + ARCHIVE_HEADER_SIZE + SOURCE_ENTRY_SIZE;
	serialized->source_path = serialized->source_name + serialized->source_name_size;
	serialized->image = archive + ARCHIVE_HEADER_SIZE + data_size;
	return memcmp(archive + ARCHIVE_SOURCE_HASH_OFFSET, serialized->view.source_hash,
				  POCO_BLAKE3_HASH_SIZE) == 0;
}

static int has_line_map(const PocoProgram* program)
{
	const Func_frame* frame;

	for (frame = program->code.functions; frame != NULL; frame = frame->next) {
		if (frame->ld != NULL && frame->ld->count > 0 && frame->ld->offsets != NULL &&
			frame->ld->lines != NULL) {
			return 1;
		}
	}
	return 0;
}

static const Func_frame* find_frame(const PocoProgram* program, const char* name)
{
	const Func_frame* frame;
	for (frame = program->code.functions; frame != NULL; frame = frame->next) {
		if (frame->name != NULL && strcmp(frame->name, name) == 0) {
			return frame;
		}
	}
	return NULL;
}

static const char* path_basename(const char* path)
{
	const char* separator = strrchr(path, '/');
	const char* alternate_separator = strrchr(path, '\\');

	if (alternate_separator != NULL && (separator == NULL || alternate_separator > separator)) {
		separator = alternate_separator;
	}
	return separator != NULL ? separator + 1 : path;
}

int main(void)
{
	static const char buffer_source[] =
		"int helper(int value) { return value * 2; }\n"
		"int main(void) { return helper(21); }\n";
	const char* source_path = POCO_DEBUG_METADATA_SOURCE;
	const char* source_name = path_basename(source_path);
	SerializedProgram minimal;
	SerializedProgram extended;
	SerializedProgram downgraded;
	PocoVm* vm = NULL;
	PocoProgram* compiled = NULL;
	PocoProgram* minimal_program = NULL;
	PocoProgram* extended_program = NULL;
	PocoProgram* buffer_program = NULL;
	uint8_t expected_hash[POCO_BLAKE3_HASH_SIZE];
	uint8_t* source = NULL;
	size_t source_size;
	long source_length = -1;
	FILE* source_file = NULL;
	int32_t minimal_result = 0;
	int32_t extended_result = 0;
	size_t rejected_size = 99;
	int ok = 1;

	memset(&minimal, 0, sizeof(minimal));
	memset(&extended, 0, sizeof(extended));
	memset(&downgraded, 0, sizeof(downgraded));
	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "VM creation failed");
	{
		PocoStatus buffer_status = poco_vm_compile_buffer(
			vm, "virtual.poc", buffer_source, sizeof(buffer_source) - 1, &buffer_program);
		if (buffer_status != POCO_STATUS_OK) {
			fprintf(stderr, "debug metadata: buffer compile status %d: %s\n", (int)buffer_status,
					poco_get_last_error(vm));
		}
		ok &= check(buffer_status == POCO_STATUS_OK, "buffer fixture compilation failed");
	}
	ok &= check(poco_vm_compile_file(vm, source_path, &compiled) == POCO_STATUS_OK,
				"file compilation failed");
	if (compiled == NULL) {
		goto cleanup;
	}
	ok &= check(serialize_program(compiled, POCO_DEBUG_LEVEL_MINIMAL, &minimal),
				"minimal serialization failed");
	ok &= check(serialize_program(compiled, POCO_DEBUG_LEVEL_EXTENDED, &extended),
				"extended serialization failed");
	if (minimal.bytes == NULL || extended.bytes == NULL) {
		goto cleanup;
	}

	ok &= check(minimal.view.debug_level == PO_BYTECODE_DEBUG_MINIMAL,
				"minimal header level mismatch");
	ok &= check(extended.view.debug_level == PO_BYTECODE_DEBUG_EXTENDED,
				"extended header level mismatch");
	ok &= check(minimal.source_path_size == 0, "minimal archive retained a source path");
	ok &= check(!contains_bytes(minimal.bytes, minimal.size, source_path, strlen(source_path)),
				"minimal container leaked the source path");
	ok &= check(extended.source_path_size == strlen(source_path) &&
					memcmp(extended.source_path, source_path, strlen(source_path)) == 0,
				"extended archive source path mismatch");
	ok &= check(minimal.source_name_size == strlen(source_name) &&
					memcmp(minimal.source_name, source_name, strlen(source_name)) == 0 &&
					extended.source_name_size == strlen(source_name) &&
					memcmp(extended.source_name, source_name, strlen(source_name)) == 0,
				"source name was not retained at both levels");
	ok &= check(po_load_le32(minimal.image + 12) == 6,
				"minimal image unexpectedly carries an extended section");
	ok &= check(po_load_le32(extended.image + 12) == 7 &&
					po_load_le32(extended.image + 160) == PO_PROGRAM_IMAGE_SECTION_DEBUG_LOCALS,
				"extended image is missing the debug-locals section");
	ok &= check(!contains_bytes(minimal.image, minimal.image_size, "doubled", 7) &&
					contains_bytes(extended.image, extended.image_size, "doubled", 7),
				"debug-local names were not gated to the extended image");

	source_file = fopen(source_path, "rb");
	ok &=
		check(source_file != NULL && fseek(source_file, 0, SEEK_END) == 0 &&
				  (source_length = ftell(source_file)) >= 0 && fseek(source_file, 0, SEEK_SET) == 0,
			  "source hash fixture could not read the source");
	if (source_file != NULL && source_length >= 0) {
		source_size = (size_t)source_length;
		source = (uint8_t*)malloc(source_size > 0 ? source_size : 1);
		ok &= check(source != NULL && (source_size == 0 ||
									   fread(source, 1, source_size, source_file) == source_size),
					"source hash fixture read failed");
		if (source != NULL) {
			po_blake3_hash(source, source_size, expected_hash);
			ok &= check(
				memcmp(minimal.view.source_hash, expected_hash, sizeof(expected_hash)) == 0 &&
					memcmp(extended.view.source_hash, expected_hash, sizeof(expected_hash)) == 0,
				"source hash was not retained at both levels");
		}
	}

	ok &= check(poco_vm_deserialize_buffer(vm, minimal.bytes, minimal.size, &minimal_program) ==
					POCO_STATUS_OK,
				"minimal deserialization failed");
	ok &= check(poco_vm_deserialize_buffer(vm, extended.bytes, extended.size, &extended_program) ==
					POCO_STATUS_OK,
				"extended deserialization failed");
	if (minimal_program != NULL && extended_program != NULL) {
		const Func_frame* minimal_helper = find_frame(minimal_program, "helper");
		const Func_frame* extended_helper = find_frame(extended_program, "helper");
		const Func_frame* extended_main = find_frame(extended_program, "main");
		const PocoDebugLocal* value;
		const PocoDebugLocal* widened;
		const PocoDebugLocal* ratio;
		const PocoDebugLocal* doubled;
		const PocoDebugLocal* pair;

		ok &= check(minimal_program->debug_level == POCO_DEBUG_LEVEL_MINIMAL &&
						minimal_program->source_path == NULL,
					"minimal decoded metadata mismatch");
		ok &= check(extended_program->debug_level == POCO_DEBUG_LEVEL_EXTENDED &&
						extended_program->source_path != NULL &&
						strcmp(extended_program->source_path, source_path) == 0,
					"extended decoded source path mismatch");
		ok &= check(has_line_map(minimal_program) && has_line_map(extended_program),
					"opcode-to-line map missing from a debug tier");
		ok &= check(minimal_helper != NULL && minimal_helper->debug_locals == NULL,
					"minimal decode reconstructed extended local metadata");
		value = po_program_debug_local_lookup(extended_helper, "value", 0, SHRT_MAX);
		widened = po_program_debug_local_lookup(extended_helper, "widened", 0, SHRT_MAX);
		ratio = po_program_debug_local_lookup(extended_helper, "ratio", 0, SHRT_MAX);
		doubled = po_program_debug_local_lookup(extended_helper, "doubled", 0, SHRT_MAX);
		pair = po_program_debug_local_lookup(extended_helper, "pair", 0, SHRT_MAX);
		ok &= check(
			value != NULL && widened != NULL && ratio != NULL && doubled != NULL && pair != NULL,
			"extended local lookup did not reconstruct every descriptor");
		ok &= check(value != NULL && value->type->ido_type == IDO_INT && widened != NULL &&
						widened->type->ido_type == IDO_LONG && ratio != NULL &&
						ratio->type->ido_type == IDO_DOUBLE && doubled != NULL &&
						doubled->type->ido_type == IDO_INT && pair != NULL &&
						pair->type->ido_type == IDO_STRUCT && pair->type->comp_count > 0 &&
						pair->type->comp[0] == TYPE_STRUCT &&
						((Struct_info*)pair->type->sdims[0].pt)->size == sizeof(int) + sizeof(long),
					"extended local types were not reconstructed");
		ok &=
			check(doubled != NULL && doubled->scope > SCOPE_GLOBAL &&
					  doubled->storage_scope == SCOPE_LOCAL && doubled->frame_offset < 0 &&
					  doubled->live_start == 0 && doubled->live_end == extended_helper->code_size &&
					  doubled->live_range_approximate,
				  "extended local scope/slot/live range metadata mismatch");
		ok &= check(
			po_program_debug_local_lookup(extended_helper, "doubled", 0, SCOPE_GLOBAL) == NULL &&
				po_program_debug_local_lookup(extended_helper, "doubled",
											  extended_helper->code_size, SHRT_MAX) == NULL &&
				po_program_debug_local_lookup(extended_main, "doubled", 0, SHRT_MAX) == NULL,
			"scope/live-range/frame-local lookup boundaries were not enforced");
		ok &=
			check(poco_vm_run(vm, minimal_program, NULL, &minimal_result) == POCO_STATUS_OK &&
					  poco_vm_run(vm, extended_program, NULL, &extended_result) == POCO_STATUS_OK &&
					  minimal_result == 42 && extended_result == 42,
				  "debug-tier run results differ");
		ok &= check(serialize_program(extended_program, POCO_DEBUG_LEVEL_MINIMAL, &downgraded),
					"extended decode could not be reserialized minimally");
		ok &= check(downgraded.image != NULL && po_load_le32(downgraded.image + 12) == 6 &&
						!contains_bytes(downgraded.image, downgraded.image_size, "LocalPair", 9) &&
						!contains_bytes(downgraded.image, downgraded.image_size, "doubled", 7),
					"minimal reserialization leaked extended-only local type/name metadata");
	}

	ok &= check(poco_program_serialize_buffer(compiled, (PocoDebugLevel)2, NULL, 0,
											  &rejected_size) == POCO_STATUS_PARAMETER_RANGE &&
					rejected_size == 0,
				"unknown debug level was accepted");
	if (buffer_program != NULL) {
		ok &= check(poco_program_serialize_buffer(buffer_program, POCO_DEBUG_LEVEL_EXTENDED, NULL,
												  0, &rejected_size) == POCO_STATUS_PARAMETER_RANGE,
					"extended serialization accepted a program without a source path");
	}

cleanup:
	if (source_file != NULL) {
		fclose(source_file);
	}
	free(source);
	free(downgraded.bytes);
	free(extended.bytes);
	free(minimal.bytes);
	poco_program_destroy(buffer_program);
	poco_program_destroy(extended_program);
	poco_program_destroy(minimal_program);
	poco_program_destroy(compiled);
	poco_vm_destroy(vm);
	if (!ok) {
		return 1;
	}
	printf("debug metadata tiers passed\n");
	return 0;
}
