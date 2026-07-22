#include <poco/poco.h>

#include "bytecode_container.h"
#include "filepath.h"
#include "poco_endian.h"
#include "poco_hash.h"
#include "program_image.h"
#include "program_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#ifdef __APPLE__
#include <fcntl.h>
#endif
#endif

enum {
	PO_SERIALIZED_ARCHIVE_HEADER_SIZE = 80,
	PO_SERIALIZED_SOURCE_ENTRY_SIZE = 48,
	PO_SERIALIZED_ARCHIVE_VERSION = 2,
	PO_SERIALIZED_ARCHIVE_FLAG_SOURCELESS = 1,
	PO_SERIALIZED_ARCHIVE_OFFSET_VERSION = 8,
	PO_SERIALIZED_ARCHIVE_OFFSET_FLAGS = 12,
	PO_SERIALIZED_ARCHIVE_OFFSET_SOURCE_COUNT = 16,
	PO_SERIALIZED_ARCHIVE_OFFSET_PRIMARY_SOURCE = 24,
	PO_SERIALIZED_ARCHIVE_OFFSET_SOURCE_TABLE_SIZE = 32,
	PO_SERIALIZED_ARCHIVE_OFFSET_IMAGE_SIZE = 40,
	PO_SERIALIZED_ARCHIVE_OFFSET_PRIMARY_HASH = 48
};

static const uint8_t po_serialized_archive_magic[8] = {'P', 'O', 'A', 'P', 'I', '0', '0', '1'};

static PocoStatus po_program_image_status(PoProgramImageStatus status)
{
	switch (status) {
		case PO_PROGRAM_IMAGE_OK:
			return POCO_STATUS_OK;
		case PO_PROGRAM_IMAGE_OUT_OF_MEMORY:
			return POCO_STATUS_OUT_OF_MEMORY;
		case PO_PROGRAM_IMAGE_TRUNCATED:
			return POCO_STATUS_IMAGE_TRUNCATED;
		case PO_PROGRAM_IMAGE_VERSION_MISMATCH:
			return POCO_STATUS_IMAGE_VERSION_MISMATCH;
		case PO_PROGRAM_IMAGE_UNSUPPORTED:
			return POCO_STATUS_UNIMPLEMENTED;
		case PO_PROGRAM_IMAGE_UNKNOWN_BINDING:
			return POCO_STATUS_FFI_FUNCTION_NOT_FOUND;
		case PO_PROGRAM_IMAGE_MODULE_NOT_FOUND:
			return POCO_STATUS_MODULE_NOT_FOUND;
		case PO_PROGRAM_IMAGE_MODULE_LOAD_FAILED:
		case PO_PROGRAM_IMAGE_EXTERNAL_LIBRARIES_DISABLED:
			return POCO_STATUS_MODULE_LOAD_FAILED;
		case PO_PROGRAM_IMAGE_MODULE_NO_ENTRY:
			return POCO_STATUS_MODULE_NO_ENTRY;
		case PO_PROGRAM_IMAGE_MODULE_INVALID:
			return POCO_STATUS_MODULE_INVALID;
		case PO_PROGRAM_IMAGE_MODULE_VERSION:
			return POCO_STATUS_MODULE_VERSION;
		case PO_PROGRAM_IMAGE_MODULE_EMPTY:
			return POCO_STATUS_MODULE_EMPTY;
		case PO_PROGRAM_IMAGE_INVALID_ARGUMENT:
			return POCO_STATUS_PARAMETER_RANGE;
		case PO_PROGRAM_IMAGE_MALFORMED:
		default:
			return POCO_STATUS_IMAGE_CORRUPT;
	}
}

static PocoStatus po_envelope_status(PoBytecodeContainerStatus status)
{
	switch (status) {
		case PO_BYTECODE_CONTAINER_OK:
			return POCO_STATUS_OK;
		case PO_BYTECODE_CONTAINER_OUT_OF_MEMORY:
			return POCO_STATUS_OUT_OF_MEMORY;
		case PO_BYTECODE_CONTAINER_INVALID_ARGUMENT:
			return POCO_STATUS_PARAMETER_RANGE;
		case PO_BYTECODE_CONTAINER_TRUNCATED:
			return POCO_STATUS_IMAGE_TRUNCATED;
		case PO_BYTECODE_CONTAINER_VERSION_MISMATCH:
			return POCO_STATUS_IMAGE_VERSION_MISMATCH;
		default:
			return POCO_STATUS_IMAGE_CORRUPT;
	}
}

static PocoStatus po_serialize_allocate(const PocoProgram* program, PocoDebugLevel debug_level,
										uint8_t** out_container, size_t* out_container_size)
{
	uint8_t* program_image = NULL;
	size_t program_image_size = 0;
	uint8_t* archive = NULL;
	size_t archive_size;
	size_t source_table_size = 0;
	size_t source_index;
	size_t table_offset;
	PoProgramImageStatus image_status;
	PoBytecodeContainerStatus envelope_status;

	if (program == NULL || out_container == NULL || out_container_size == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	*out_container = NULL;
	*out_container_size = 0;
	if (debug_level != POCO_DEBUG_LEVEL_MINIMAL && debug_level != POCO_DEBUG_LEVEL_EXTENDED) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	if (program->sources == NULL || program->source_count == 0 ||
		program->primary_source_index >= program->source_count) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	if (!poco_hash_equal(program->source_hash,
					 program->sources[program->primary_source_index].hash)) {
		return POCO_STATUS_IMAGE_CORRUPT;
	}
	for (source_index = 0; source_index < program->source_count; ++source_index) {
		size_t name_size;
		size_t path_size;
		if (program->sources[source_index].name == NULL ||
			(debug_level == POCO_DEBUG_LEVEL_EXTENDED &&
			 (program->sources[source_index].path == NULL ||
			  program->sources[source_index].path[0] == '\0'))) {
			return POCO_STATUS_PARAMETER_RANGE;
		}
		name_size = strlen(program->sources[source_index].name);
		path_size = debug_level == POCO_DEBUG_LEVEL_EXTENDED
						? strlen(program->sources[source_index].path)
						: 0;
		if (name_size > SIZE_MAX - PO_SERIALIZED_SOURCE_ENTRY_SIZE ||
			path_size > SIZE_MAX - PO_SERIALIZED_SOURCE_ENTRY_SIZE - name_size ||
			source_table_size >
				SIZE_MAX - PO_SERIALIZED_SOURCE_ENTRY_SIZE - name_size - path_size) {
			return POCO_STATUS_OVERFLOW;
		}
		source_table_size += PO_SERIALIZED_SOURCE_ENTRY_SIZE + name_size + path_size;
	}
	{
		const Func_frame* frame;
		for (frame = program->code.functions; frame != NULL; frame = frame->next) {
			if (frame->unit_index >= program->source_count) {
				return POCO_STATUS_IMAGE_CORRUPT;
			}
		}
		for (frame = program->code.prototypes; frame != NULL; frame = frame->next) {
			if (frame->unit_index >= program->source_count) {
				return POCO_STATUS_IMAGE_CORRUPT;
			}
		}
	}
	image_status =
		po_program_image_encode(program, debug_level, &program_image, &program_image_size);
	if (image_status != PO_PROGRAM_IMAGE_OK) {
		return po_program_image_status(image_status);
	}
	if (source_table_size > SIZE_MAX - PO_SERIALIZED_ARCHIVE_HEADER_SIZE ||
		program_image_size > SIZE_MAX - PO_SERIALIZED_ARCHIVE_HEADER_SIZE - source_table_size) {
		free(program_image);
		return POCO_STATUS_OVERFLOW;
	}
	archive_size = PO_SERIALIZED_ARCHIVE_HEADER_SIZE + source_table_size + program_image_size;
	archive = (uint8_t*)malloc(archive_size);
	if (archive == NULL) {
		free(program_image);
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	memcpy(archive, po_serialized_archive_magic, sizeof(po_serialized_archive_magic));
	po_store_le32(archive + PO_SERIALIZED_ARCHIVE_OFFSET_VERSION, PO_SERIALIZED_ARCHIVE_VERSION);
	po_store_le32(archive + PO_SERIALIZED_ARCHIVE_OFFSET_FLAGS,
				  PO_SERIALIZED_ARCHIVE_FLAG_SOURCELESS);
	po_store_le64(archive + PO_SERIALIZED_ARCHIVE_OFFSET_SOURCE_COUNT,
				  (uint64_t)program->source_count);
	po_store_le64(archive + PO_SERIALIZED_ARCHIVE_OFFSET_PRIMARY_SOURCE,
				  (uint64_t)program->primary_source_index);
	po_store_le64(archive + PO_SERIALIZED_ARCHIVE_OFFSET_SOURCE_TABLE_SIZE,
				  (uint64_t)source_table_size);
	po_store_le64(archive + PO_SERIALIZED_ARCHIVE_OFFSET_IMAGE_SIZE, (uint64_t)program_image_size);
	memcpy(archive + PO_SERIALIZED_ARCHIVE_OFFSET_PRIMARY_HASH, program->source_hash,
		   sizeof(program->source_hash));
	table_offset = PO_SERIALIZED_ARCHIVE_HEADER_SIZE;
	for (source_index = 0; source_index < program->source_count; ++source_index) {
		const PocoProgramSource* source = &program->sources[source_index];
		size_t name_size = strlen(source->name);
		size_t path_size = debug_level == POCO_DEBUG_LEVEL_EXTENDED ? strlen(source->path) : 0;
		po_store_le64(archive + table_offset, (uint64_t)name_size);
		po_store_le64(archive + table_offset + 8, (uint64_t)path_size);
		memcpy(archive + table_offset + 16, source->hash, sizeof(source->hash));
		table_offset += PO_SERIALIZED_SOURCE_ENTRY_SIZE;
		memcpy(archive + table_offset, source->name, name_size);
		table_offset += name_size;
		if (path_size != 0) {
			memcpy(archive + table_offset, source->path, path_size);
			table_offset += path_size;
		}
	}
	memcpy(archive + table_offset, program_image, program_image_size);
	free(program_image);
	envelope_status = po_bytecode_envelope_write_hash(program->source_hash, archive, archive_size,
													  (PoBytecodeDebugLevel)debug_level,
													  out_container, out_container_size);
	free(archive);
	return po_envelope_status(envelope_status);
}

static PocoStatus po_serialize_to_buffer(const PocoProgram* program, PocoDebugLevel debug_level,
										 void* buffer, size_t buffer_size, size_t* out_size)
{
	uint8_t* container = NULL;
	size_t container_size = 0;
	PocoStatus status;

	if (out_size == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	*out_size = 0;
	status = po_serialize_allocate(program, debug_level, &container, &container_size);
	if (status != POCO_STATUS_OK) {
		return status;
	}
	*out_size = container_size;
	if (buffer == NULL || buffer_size < container_size) {
		free(container);
		return POCO_STATUS_BUFFER_TOO_SMALL;
	}
	memcpy(buffer, container, container_size);
	free(container);
	return POCO_STATUS_OK;
}

static PocoStatus po_serialize_to_file(const PocoProgram* program, PocoDebugLevel debug_level,
									   FILE* file)
{
	uint8_t* container = NULL;
	size_t container_size = 0;
	PocoStatus status;

	if (program == NULL || file == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	status = po_serialize_allocate(program, debug_level, &container, &container_size);
	if (status != POCO_STATUS_OK) {
		return status;
	}
	if (container_size > 0 && fwrite(container, 1, container_size, file) != container_size) {
		free(container);
		return POCO_STATUS_WRITE_FAILED;
	}
	free(container);
	return POCO_STATUS_OK;
}

PocoStatus poco_program_serialize_buffer(const PocoProgram* program, PocoDebugLevel debug_level,
										 void* buffer, size_t buffer_size, size_t* out_size)
{
	return po_serialize_to_buffer(program, debug_level, buffer, buffer_size, out_size);
}

PocoStatus poco_program_serialize_file(const PocoProgram* program, PocoDebugLevel debug_level,
									   FILE* file)
{
	return po_serialize_to_file(program, debug_level, file);
}

static PocoStatus po_vm_deserialize_buffer_impl(PocoVm* vm, const void* buffer, size_t buffer_size,
												const char* script_path, PocoProgram** out_program)
{
	PoBytecodeEnvelopeView view;
	const uint8_t* archive;
	uint64_t source_count64;
	uint64_t primary_source64;
	uint64_t source_table_size64;
	uint64_t image_size64;
	uint32_t archive_flags;
	size_t source_count;
	size_t primary_source;
	size_t source_table_size;
	size_t image_size;
	const uint8_t* source_table;
	const uint8_t* image;
	size_t source_offset = 0;
	size_t source_index;
	PocoProgram* program = NULL;
	PocoStatus status;
	PoBytecodeContainerStatus envelope_status;
	PoProgramImageStatus image_status;

	if (vm == NULL || out_program == NULL || (buffer == NULL && buffer_size != 0)) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	*out_program = NULL;
	envelope_status = po_bytecode_envelope_read(buffer, buffer_size, &view);
	if (envelope_status != PO_BYTECODE_CONTAINER_OK) {
		return po_envelope_status(envelope_status);
	}
	if (view.image_size < PO_SERIALIZED_ARCHIVE_HEADER_SIZE) {
		return POCO_STATUS_IMAGE_TRUNCATED;
	}
	archive = view.image;
	archive_flags = po_load_le32(archive + PO_SERIALIZED_ARCHIVE_OFFSET_FLAGS);
	if (memcmp(archive, po_serialized_archive_magic, sizeof(po_serialized_archive_magic)) != 0 ||
		archive_flags != PO_SERIALIZED_ARCHIVE_FLAG_SOURCELESS) {
		return POCO_STATUS_IMAGE_CORRUPT;
	}
	if (po_load_le32(archive + PO_SERIALIZED_ARCHIVE_OFFSET_VERSION) !=
		PO_SERIALIZED_ARCHIVE_VERSION) {
		return POCO_STATUS_IMAGE_VERSION_MISMATCH;
	}
	source_count64 = po_load_le64(archive + PO_SERIALIZED_ARCHIVE_OFFSET_SOURCE_COUNT);
	primary_source64 = po_load_le64(archive + PO_SERIALIZED_ARCHIVE_OFFSET_PRIMARY_SOURCE);
	source_table_size64 = po_load_le64(archive + PO_SERIALIZED_ARCHIVE_OFFSET_SOURCE_TABLE_SIZE);
	image_size64 = po_load_le64(archive + PO_SERIALIZED_ARCHIVE_OFFSET_IMAGE_SIZE);
	if (source_count64 == 0 || source_count64 > SIZE_MAX || primary_source64 >= source_count64 ||
		source_table_size64 > SIZE_MAX || image_size64 > SIZE_MAX) {
		return POCO_STATUS_IMAGE_CORRUPT;
	}
	source_count = (size_t)source_count64;
	primary_source = (size_t)primary_source64;
	source_table_size = (size_t)source_table_size64;
	image_size = (size_t)image_size64;
	if (source_count > source_table_size / PO_SERIALIZED_SOURCE_ENTRY_SIZE) {
		return POCO_STATUS_IMAGE_CORRUPT;
	}
	if (!poco_hash_equal(archive + PO_SERIALIZED_ARCHIVE_OFFSET_PRIMARY_HASH, view.source_hash)) {
		return POCO_STATUS_IMAGE_CORRUPT;
	}
	if (source_table_size > view.image_size - PO_SERIALIZED_ARCHIVE_HEADER_SIZE ||
		image_size > view.image_size - PO_SERIALIZED_ARCHIVE_HEADER_SIZE - source_table_size) {
		return POCO_STATUS_IMAGE_TRUNCATED;
	}
	if (image_size != view.image_size - PO_SERIALIZED_ARCHIVE_HEADER_SIZE - source_table_size) {
		return POCO_STATUS_IMAGE_CORRUPT;
	}
	source_table = archive + PO_SERIALIZED_ARCHIVE_HEADER_SIZE;
	image = source_table + source_table_size;
	image_status = po_program_image_decode(vm, image, image_size, (PocoDebugLevel)view.debug_level,
										   script_path, &program);
	if (image_status != PO_PROGRAM_IMAGE_OK) {
		return po_program_image_status(image_status);
	}
	if (source_count > SIZE_MAX / sizeof(*program->sources)) {
		poco_program_destroy(program);
		return POCO_STATUS_IMAGE_CORRUPT;
	}
	program->sources = calloc(source_count, sizeof(*program->sources));
	if (program->sources == NULL) {
		poco_program_destroy(program);
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	program->source_count = source_count;
	program->primary_source_index = primary_source;
	for (source_index = 0; source_index < source_count; ++source_index) {
		uint64_t name_size64;
		uint64_t path_size64;
		size_t name_size;
		size_t path_size;
		PocoProgramSource* source = &program->sources[source_index];
		if (source_table_size - source_offset < PO_SERIALIZED_SOURCE_ENTRY_SIZE) {
			poco_program_destroy(program);
			return POCO_STATUS_IMAGE_CORRUPT;
		}
		name_size64 = po_load_le64(source_table + source_offset);
		path_size64 = po_load_le64(source_table + source_offset + 8);
		if (name_size64 > SIZE_MAX || path_size64 > SIZE_MAX) {
			poco_program_destroy(program);
			return POCO_STATUS_IMAGE_CORRUPT;
		}
		name_size = (size_t)name_size64;
		path_size = (size_t)path_size64;
		if ((view.debug_level == PO_BYTECODE_DEBUG_MINIMAL && path_size != 0) ||
			(view.debug_level == PO_BYTECODE_DEBUG_EXTENDED && path_size == 0) ||
			name_size > source_table_size - source_offset - PO_SERIALIZED_SOURCE_ENTRY_SIZE ||
			path_size >
				source_table_size - source_offset - PO_SERIALIZED_SOURCE_ENTRY_SIZE - name_size) {
			poco_program_destroy(program);
			return POCO_STATUS_IMAGE_CORRUPT;
		}
		memcpy(source->hash, source_table + source_offset + 16, sizeof(source->hash));
		source_offset += PO_SERIALIZED_SOURCE_ENTRY_SIZE;
		if (memchr(source_table + source_offset, '\0', name_size) != NULL ||
			memchr(source_table + source_offset + name_size, '\0', path_size) != NULL) {
			poco_program_destroy(program);
			return POCO_STATUS_IMAGE_CORRUPT;
		}
		source->name = malloc(name_size + 1);
		source->path = path_size != 0 ? malloc(path_size + 1) : NULL;
		if (source->name == NULL || (path_size != 0 && source->path == NULL)) {
			poco_program_destroy(program);
			return POCO_STATUS_OUT_OF_MEMORY;
		}
		memcpy(source->name, source_table + source_offset, name_size);
		source->name[name_size] = '\0';
		source_offset += name_size;
		if (path_size != 0) {
			memcpy(source->path, source_table + source_offset, path_size);
			source->path[path_size] = '\0';
			source_offset += path_size;
		}
	}
	if (source_offset != source_table_size ||
		!poco_hash_equal(program->sources[primary_source].hash, view.source_hash)) {
		poco_program_destroy(program);
		return POCO_STATUS_IMAGE_CORRUPT;
	}
	{
		const Func_frame* frame;
		for (frame = program->code.functions; frame != NULL; frame = frame->next) {
			if (frame->unit_index >= source_count) {
				poco_program_destroy(program);
				return POCO_STATUS_IMAGE_CORRUPT;
			}
		}
		for (frame = program->code.prototypes; frame != NULL; frame = frame->next) {
			if (frame->unit_index >= source_count) {
				poco_program_destroy(program);
				return POCO_STATUS_IMAGE_CORRUPT;
			}
		}
	}
	program->source_name = strdup(program->sources[primary_source].name);
	program->source_path = program->sources[primary_source].path != NULL
							   ? strdup(program->sources[primary_source].path)
							   : NULL;
	if (program->source_name == NULL ||
		(program->sources[primary_source].path != NULL && program->source_path == NULL)) {
		poco_program_destroy(program);
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	program->debug_level = (PocoDebugLevel)view.debug_level;
	memcpy(program->source_hash, view.source_hash, sizeof(program->source_hash));
	status = POCO_STATUS_OK;
	*out_program = program;
	return status;
}

PocoStatus poco_vm_deserialize_buffer(PocoVm* vm, const void* buffer, size_t buffer_size,
									  PocoProgram** out_program)
{
	return po_vm_deserialize_buffer_impl(vm, buffer, buffer_size, NULL, out_program);
}

static const char* po_deserialize_file_path(FILE* file, char* path, size_t path_size)
{
	if (file == NULL || path == NULL || path_size == 0) {
		return NULL;
	}
#ifdef _WIN32
	{
		intptr_t descriptor = _get_osfhandle(_fileno(file));
		DWORD length;

		if (descriptor == -1) {
			return NULL;
		}
		length = GetFinalPathNameByHandleA((HANDLE)descriptor, path, (DWORD)path_size,
										   FILE_NAME_NORMALIZED);
		if (length == 0 || length >= path_size) {
			return NULL;
		}
		return path;
	}
#elif defined(__APPLE__)
	return fcntl(fileno(file), F_GETPATH, path) == 0 ? path : NULL;
#else
	{
		char descriptor_path[64];
		int descriptor_length =
			snprintf(descriptor_path, sizeof(descriptor_path), "/proc/self/fd/%d", fileno(file));
		ssize_t length;

		if (descriptor_length <= 0 || (size_t)descriptor_length >= sizeof(descriptor_path)) {
			return NULL;
		}
		length = readlink(descriptor_path, path, path_size - 1);
		if (length <= 0 || (size_t)length >= path_size - 1) {
			return NULL;
		}
		path[length] = '\0';
		return path;
	}
#endif
}

PocoStatus poco_vm_deserialize_file(PocoVm* vm, FILE* file, PocoProgram** out_program)
{
	long start;
	long end;
	size_t size;
	uint8_t* buffer;
	char script_path[PATH_SIZE];
	const char* resolved_script_path;
	PocoStatus status;

	if (vm == NULL || file == NULL || out_program == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	*out_program = NULL;
	start = ftell(file);
	if (start < 0 || fseek(file, 0, SEEK_END) != 0 || (end = ftell(file)) < start ||
		fseek(file, start, SEEK_SET) != 0) {
		return POCO_STATUS_SEEK_FAILED;
	}
	size = (size_t)(end - start);
	if ((long)size != end - start) {
		return POCO_STATUS_OVERFLOW;
	}
	buffer = (uint8_t*)malloc(size > 0 ? size : 1);
	if (buffer == NULL) {
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	if (size > 0 && fread(buffer, 1, size, file) != size) {
		free(buffer);
		return POCO_STATUS_READ_FAILED;
	}
	resolved_script_path = po_deserialize_file_path(file, script_path, sizeof(script_path));
	status = po_vm_deserialize_buffer_impl(vm, buffer, size, resolved_script_path, out_program);
	free(buffer);
	return status;
}
