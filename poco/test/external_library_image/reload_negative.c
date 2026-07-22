#include "bytecode_container.h"
#include "poco_endian.h"

#include <poco/poco.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	ARCHIVE_HEADER_SIZE = 80,
	ARCHIVE_SOURCE_TABLE_SIZE_OFFSET = 32,
	IMAGE_EXTERNAL_SECTION_OFFSET = 160,
	IMAGE_SECTION_DATA_OFFSET = 8
};

static int check(int condition, const char* message)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "external library reload: %s\n", message);
	return 0;
}

static PocoStatus count_load(void* user_data, const PocoModuleInfo* info)
{
	int* count = user_data;

	(void)info;
	++*count;
	return POCO_STATUS_OK;
}

static int serialize_program(PocoProgram* program, uint8_t** out_bytes, size_t* out_size)
{
	PocoStatus status;

	*out_bytes = NULL;
	*out_size = 0;
	status = poco_program_serialize_buffer(program, POCO_DEBUG_LEVEL_MINIMAL, NULL, 0, out_size);
	if (status != POCO_STATUS_BUFFER_TOO_SMALL) {
		return 0;
	}
	*out_bytes = malloc(*out_size);
	if (*out_bytes == NULL) {
		return 0;
	}
	return poco_program_serialize_buffer(program, POCO_DEBUG_LEVEL_MINIMAL, *out_bytes, *out_size,
										 out_size) == POCO_STATUS_OK;
}

static int retarget_library(const uint8_t* bytes, size_t size, const char* replacement,
							uint8_t** out_bytes, size_t* out_size)
{
	PoBytecodeEnvelopeView view;
	uint8_t* archive;
	uint8_t* image;
	size_t source_table_size;
	size_t library_offset;
	size_t name_size;
	PoBytecodeContainerStatus status;

	*out_bytes = NULL;
	*out_size = 0;
	if (po_bytecode_envelope_read(bytes, size, &view) != PO_BYTECODE_CONTAINER_OK ||
		view.image_size < ARCHIVE_HEADER_SIZE) {
		return 0;
	}
	archive = malloc(view.image_size);
	if (archive == NULL) {
		return 0;
	}
	memcpy(archive, view.image, view.image_size);
	source_table_size = (size_t)po_load_le64(archive + ARCHIVE_SOURCE_TABLE_SIZE_OFFSET);
	if (source_table_size > view.image_size - ARCHIVE_HEADER_SIZE) {
		free(archive);
		return 0;
	}
	image = archive + ARCHIVE_HEADER_SIZE + source_table_size;
	library_offset = (size_t)po_load_le64(image + IMAGE_EXTERNAL_SECTION_OFFSET + 8);
	name_size = (size_t)po_load_le32(image + library_offset + 4);
	if (strlen(replacement) != name_size) {
		free(archive);
		return 0;
	}
	memcpy(image + library_offset + IMAGE_SECTION_DATA_OFFSET, replacement, name_size);
	status = po_bytecode_envelope_write_hash(view.source_hash, archive, view.image_size,
											 view.debug_level, out_bytes, out_size);
	free(archive);
	return status == PO_BYTECODE_CONTAINER_OK;
}

static int expect_load_status(const uint8_t* bytes, size_t size, PocoStatus expected,
							  const char* message)
{
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	PocoStatus status;
	int ok = 1;

	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "negative VM creation failed");
	ok &= check(
		poco_vm_add_library_path(vm, POCO_EXTERNAL_LIBRARY_IMAGE_MODULE_DIR) == POCO_STATUS_OK,
		"negative module path registration failed");
	status = poco_vm_deserialize_buffer(vm, bytes, size, &program);
	ok &= check(status == expected && program == NULL, message);
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return ok;
}

int main(void)
{
	static const char external_source[] =
		"#pragma poco library \"serialize_fixture.poe\"\n"
		"int main(void) { return ExternalValue(); }\n";
	static const char builtin_source[] = "int main(void) { return 7; }\n";
	static const char file_leg_source[] =
		"#pragma poco library \"fileleg___fixture.poe\"\n"
		"int main(void) { return ExternalValue(); }\n";
	PocoVm* producer = NULL;
	PocoVm* builtin_producer = NULL;
	PocoVm* file_producer = NULL;
	PocoVm* file_consumer = NULL;
	PocoVm* buffer_consumer = NULL;
	PocoVm* disabled_vm = NULL;
	PocoProgram* external_program = NULL;
	PocoProgram* builtin_program = NULL;
	PocoProgram* rejected = NULL;
	PocoProgram* decoded_builtin = NULL;
	PocoProgram* file_program = NULL;
	PocoProgram* file_decoded = NULL;
	uint8_t* external_bytes = NULL;
	uint8_t* builtin_bytes = NULL;
	uint8_t* missing_bytes = NULL;
	uint8_t* bad_abi_bytes = NULL;
	uint8_t* missing_symbol_bytes = NULL;
	uint8_t* file_bytes = NULL;
	size_t external_size = 0;
	size_t builtin_size = 0;
	size_t missing_size = 0;
	size_t bad_abi_size = 0;
	size_t missing_symbol_size = 0;
	size_t file_size = 0;
	char binary_path[FILENAME_MAX];
	FILE* binary_file = NULL;
	int load_count = 0;
	PocoModuleHooks hooks = {count_load, NULL, &load_count, 0};
	PocoVmOptions options = {0};
	int32_t result = 0;
	int ok = 1;

	ok &= check(poco_vm_create(NULL, &producer) == POCO_STATUS_OK, "producer VM creation failed");
	ok &= check(poco_vm_add_library_path(producer, POCO_EXTERNAL_LIBRARY_IMAGE_MODULE_DIR) ==
					POCO_STATUS_OK,
				"producer module path registration failed");
	ok &= check(
		poco_vm_compile_buffer(producer, "reload-negative.poc", external_source,
							   sizeof(external_source) - 1, &external_program) == POCO_STATUS_OK,
		"external program compilation failed");
	ok &= check(external_program != NULL &&
					serialize_program(external_program, &external_bytes, &external_size),
				"external program serialization failed");
	ok &= check(poco_vm_create(NULL, &builtin_producer) == POCO_STATUS_OK,
				"builtin producer VM creation failed");
	ok &= check(poco_vm_register_standard_library(builtin_producer) == POCO_STATUS_OK,
				"builtin producer standard-library registration failed");
	ok &= check(
		poco_vm_compile_buffer(builtin_producer, "builtin-only.poc", builtin_source,
							   sizeof(builtin_source) - 1, &builtin_program) == POCO_STATUS_OK,
		"builtin-only compilation failed");
	ok &= check(builtin_program != NULL &&
					serialize_program(builtin_program, &builtin_bytes, &builtin_size),
				"builtin-only serialization failed");
	ok &= check(poco_vm_create(NULL, &file_producer) == POCO_STATUS_OK,
				"file-leg producer VM creation failed");
	ok &= check(
		poco_vm_add_library_path(file_producer, POCO_EXTERNAL_LIBRARY_FILE_DIR) == POCO_STATUS_OK,
		"file-leg producer path registration failed");
	ok &=
		check(poco_vm_compile_buffer(file_producer, "file-leg.poc", file_leg_source,
									 sizeof(file_leg_source) - 1, &file_program) == POCO_STATUS_OK,
			  "file-leg compilation failed");
	ok &= check(file_program != NULL && serialize_program(file_program, &file_bytes, &file_size),
				"file-leg serialization failed");
	if (external_bytes == NULL || builtin_bytes == NULL) {
		goto CLEANUP;
	}

	options.module_hooks = &hooks;
	ok &= check(poco_vm_create(&options, &disabled_vm) == POCO_STATUS_OK,
				"disabled VM creation failed");
	ok &= check(poco_vm_add_library_path(disabled_vm, POCO_EXTERNAL_LIBRARY_IMAGE_MODULE_DIR) ==
					POCO_STATUS_OK,
				"disabled VM module path registration failed");
	ok &= check(poco_vm_register_standard_library(disabled_vm) == POCO_STATUS_OK,
				"disabled VM standard-library registration failed");
	poco_vm_disable_poe_libraries(disabled_vm);
	ok &= check(poco_vm_deserialize_buffer(disabled_vm, external_bytes, external_size, &rejected) ==
						POCO_STATUS_MODULE_LOAD_FAILED &&
					rejected == NULL && load_count == 0 &&
					strstr(poco_get_last_error(disabled_vm), "external libraries disabled") != NULL,
				"disabled VM did not refuse before the module-load hook");
	ok &= check(poco_vm_deserialize_buffer(disabled_vm, builtin_bytes, builtin_size,
										   &decoded_builtin) == POCO_STATUS_OK,
				"disabled VM refused a builtin-only image");
	ok &= check(
		poco_vm_run(disabled_vm, decoded_builtin, NULL, &result) == POCO_STATUS_OK && result == 7,
		"builtin-only image did not run on a disabled VM");

	ok &= check(retarget_library(external_bytes, external_size, "absent____fixture.poe",
								 &missing_bytes, &missing_size),
				"missing-module image mutation failed");
	ok &= check(retarget_library(external_bytes, external_size, "badabi____fixture.poe",
								 &bad_abi_bytes, &bad_abi_size),
				"bad-ABI image mutation failed");
	ok &= check(retarget_library(external_bytes, external_size, "misssym___fixture.poe",
								 &missing_symbol_bytes, &missing_symbol_size),
				"missing-symbol image mutation failed");
	if (missing_bytes != NULL) {
		ok &= expect_load_status(missing_bytes, missing_size, POCO_STATUS_MODULE_NOT_FOUND,
								 "missing module did not return MODULE_NOT_FOUND");
	}
	if (missing_symbol_bytes != NULL) {
		ok &= expect_load_status(missing_symbol_bytes, missing_symbol_size,
								 POCO_STATUS_FFI_FUNCTION_NOT_FOUND,
								 "missing symbol did not return UNKNOWN_BINDING");
	}
	if (bad_abi_bytes != NULL) {
		ok &= expect_load_status(bad_abi_bytes, bad_abi_size, POCO_STATUS_MODULE_VERSION,
								 "ABI mismatch did not return MODULE_VERSION");
	}

	if (file_bytes != NULL) {
		ok &= check(poco_vm_create(NULL, &buffer_consumer) == POCO_STATUS_OK,
					"buffer consumer VM creation failed");
		ok &= check(poco_vm_deserialize_buffer(buffer_consumer, file_bytes, file_size, &rejected) ==
							POCO_STATUS_MODULE_NOT_FOUND &&
						rejected == NULL,
					"buffer deserialize incorrectly used a script-directory search leg");
		ok &= check(snprintf(binary_path, sizeof(binary_path), "%s/reload-file-leg.pex",
							 POCO_EXTERNAL_LIBRARY_FILE_DIR) < (int)sizeof(binary_path),
					"file-leg binary path overflowed");
		binary_file = fopen(binary_path, "wb");
		ok &=
			check(binary_file != NULL && fwrite(file_bytes, 1, file_size, binary_file) == file_size,
				  "file-leg binary write failed");
		if (binary_file != NULL) {
			fclose(binary_file);
			binary_file = NULL;
		}
		binary_file = fopen(binary_path, "rb");
		ok &= check(poco_vm_create(NULL, &file_consumer) == POCO_STATUS_OK,
					"file consumer VM creation failed");
		ok &=
			check(binary_file != NULL && poco_vm_deserialize_file(file_consumer, binary_file,
																  &file_decoded) == POCO_STATUS_OK,
				  "file deserialize did not search the binary directory");
	}

CLEANUP:
	if (binary_file != NULL) {
		fclose(binary_file);
	}
	poco_program_destroy(file_decoded);
	poco_vm_destroy(file_consumer);
	poco_vm_destroy(buffer_consumer);
	poco_program_destroy(decoded_builtin);
	poco_program_destroy(rejected);
	poco_vm_destroy(disabled_vm);
	poco_program_destroy(builtin_program);
	poco_vm_destroy(builtin_producer);
	poco_program_destroy(file_program);
	poco_vm_destroy(file_producer);
	poco_program_destroy(external_program);
	poco_vm_destroy(producer);
	free(missing_symbol_bytes);
	free(bad_abi_bytes);
	free(missing_bytes);
	free(builtin_bytes);
	free(external_bytes);
	free(file_bytes);
	if (!ok) {
		return 1;
	}
	printf("external library reload negatives passed\n");
	return 0;
}
