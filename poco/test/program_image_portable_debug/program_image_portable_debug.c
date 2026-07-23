#include <poco/poco.h>

#include "bytecode_container.h"
#include "program_image.h"
#include "program_internal.h"

#include "poco_test_bytes.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	ARCHIVE_HEADER_SIZE = 80,
	ARCHIVE_SOURCE_TABLE_SIZE_OFFSET = 32,
	ARCHIVE_IMAGE_SIZE_OFFSET = 40,
	IMAGE_SECTION_COUNT_OFFSET = 12,
	IMAGE_SECTION_TABLE_OFFSET = 40,
	IMAGE_SECTION_ENTRY_SIZE = 24,
	IMAGE_SECTION_KIND_OFFSET = 0,
	IMAGE_SECTION_DATA_OFFSET = 8,
	IMAGE_SECTION_SIZE_OFFSET = 16
};

typedef struct PortableBinary {
	uint8_t* bytes;
	size_t size;
	size_t image_offset;
	size_t image_size;
} PortableBinary;

typedef struct Reader {
	const uint8_t* data;
	size_t size;
	size_t offset;
} Reader;

static int check(int condition, const char* message)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "portable debug: %s\n", message);
	return 0;
}

static int reader_u32(Reader* reader, uint32_t* value)
{
	if (reader->offset > reader->size || reader->size - reader->offset < 4) {
		return 0;
	}
	*value = read_u32(reader->data + reader->offset);
	reader->offset += 4;
	return 1;
}

static int reader_skip(Reader* reader, size_t size)
{
	if (reader->offset > reader->size || size > reader->size - reader->offset) {
		return 0;
	}
	reader->offset += size;
	return 1;
}

static int serialize_extended(const PocoProgram* program, PortableBinary* binary)
{
	PoBytecodeEnvelopeView view;
	const uint8_t* archive;
	uint64_t source_table_size;
	uint64_t image_size;
	size_t archive_offset;

	memset(binary, 0, sizeof(*binary));
	if (poco_program_serialize_buffer(program, POCO_DEBUG_LEVEL_EXTENDED, NULL, 0, &binary->size) !=
			POCO_STATUS_BUFFER_TOO_SMALL ||
		binary->size == 0) {
		return 0;
	}
	binary->bytes = (uint8_t*)malloc(binary->size);
	if (binary->bytes == NULL ||
		poco_program_serialize_buffer(program, POCO_DEBUG_LEVEL_EXTENDED, binary->bytes,
									  binary->size, &binary->size) != POCO_STATUS_OK ||
		po_bytecode_envelope_read(binary->bytes, binary->size, &view) != PO_BYTECODE_CONTAINER_OK ||
		view.debug_level != PO_BYTECODE_DEBUG_EXTENDED || view.image_size < ARCHIVE_HEADER_SIZE) {
		return 0;
	}
	archive = view.image;
	archive_offset = (size_t)(archive - binary->bytes);
	source_table_size = read_u64(archive + ARCHIVE_SOURCE_TABLE_SIZE_OFFSET);
	image_size = read_u64(archive + ARCHIVE_IMAGE_SIZE_OFFSET);
	if (source_table_size > SIZE_MAX || image_size > SIZE_MAX ||
		(size_t)source_table_size > view.image_size - ARCHIVE_HEADER_SIZE ||
		(size_t)image_size != view.image_size - ARCHIVE_HEADER_SIZE - (size_t)source_table_size) {
		return 0;
	}
	binary->image_offset = archive_offset + ARCHIVE_HEADER_SIZE + (size_t)source_table_size;
	binary->image_size = (size_t)image_size;
	return binary->image_offset <= binary->size &&
		   binary->image_size <= binary->size - binary->image_offset;
}

static int same_type(const Type_info* left, const Type_info* right);

static int same_struct(const Struct_info* left, const Struct_info* right)
{
	const Symbol* left_member;
	const Symbol* right_member;

	if (left == NULL || right == NULL || left->type != right->type ||
		strcmp(left->name, right->name) != 0 || left->size != right->size ||
		left->el_count != right->el_count) {
		return 0;
	}
	left_member = left->elements;
	right_member = right->elements;
	while (left_member != NULL && right_member != NULL) {
		if (strcmp(left_member->name, right_member->name) != 0 ||
			left_member->symval.doff != right_member->symval.doff ||
			!same_type(left_member->ti, right_member->ti)) {
			return 0;
		}
		left_member = left_member->next;
		right_member = right_member->next;
	}
	return left_member == NULL && right_member == NULL;
}

static int same_type(const Type_info* left, const Type_info* right)
{
	unsigned int index;

	if (left == NULL || right == NULL || left->comp_count != right->comp_count ||
		left->flags != right->flags || left->ido_type != right->ido_type) {
		return 0;
	}
	for (index = 0; index < left->comp_count; ++index) {
		if (left->comp[index] != right->comp[index]) {
			return 0;
		}
		if (left->comp[index] == TYPE_STRUCT) {
			if (!same_struct(left->sdims[index].pt, right->sdims[index].pt)) {
				return 0;
			}
		} else if (left->comp[index] == TYPE_FUNCTION) {
			const Func_frame* left_frame = left->sdims[index].pt;
			const Func_frame* right_frame = right->sdims[index].pt;
			if ((left_frame == NULL) != (right_frame == NULL) ||
				(left_frame != NULL && strcmp(left_frame->name, right_frame->name) != 0)) {
				return 0;
			}
		} else if (left->sdims[index].l != right->sdims[index].l) {
			return 0;
		}
	}
	return 1;
}

static int same_line_map(const Func_frame* left, const Func_frame* right)
{
	int index;

	if ((left->ld == NULL) != (right->ld == NULL)) {
		return 0;
	}
	if (left->ld == NULL) {
		return 1;
	}
	if (left->ld->count != right->ld->count) {
		return 0;
	}
	for (index = 0; index < left->ld->count; ++index) {
		if (left->ld->offsets[index] != right->ld->offsets[index] ||
			left->ld->lines[index] != right->ld->lines[index]) {
			return 0;
		}
	}
	return 1;
}

static int same_debug_locals(const Func_frame* left, const Func_frame* right, int* compared_locals,
							 int* compared_struct_local)
{
	const PocoDebugLocal* left_local = left->debug_locals;
	const PocoDebugLocal* right_local = right->debug_locals;

	while (left_local != NULL && right_local != NULL) {
		if (strcmp(left_local->name, right_local->name) != 0 ||
			left_local->frame_offset != right_local->frame_offset ||
			left_local->live_start != right_local->live_start ||
			left_local->live_end != right_local->live_end ||
			left_local->scope != right_local->scope ||
			left_local->storage_scope != right_local->storage_scope ||
			left_local->live_range_approximate != right_local->live_range_approximate ||
			!same_type(left_local->type, right_local->type)) {
			return 0;
		}
		++*compared_locals;
		if (left_local->type != NULL && left_local->type->ido_type == IDO_STRUCT) {
			*compared_struct_local = 1;
		}
		left_local = left_local->next;
		right_local = right_local->next;
	}
	return left_local == NULL && right_local == NULL;
}

static int same_debug_frames(const Func_frame* left, const Func_frame* right, int* compared_lines,
							 int* compared_locals, int* compared_struct_local)
{
	while (left != NULL && right != NULL) {
		if (strcmp(left->name, right->name) != 0 || !same_line_map(left, right) ||
			!same_debug_locals(left, right, compared_locals, compared_struct_local)) {
			return 0;
		}
		if (left->ld != NULL) {
			*compared_lines += left->ld->count;
		}
		left = left->next;
		right = right->next;
	}
	return left == NULL && right == NULL;
}

static int locate_debug_type_component(const uint8_t* image, size_t image_size,
									   size_t* out_component_offset)
{
	uint32_t section_count;
	uint32_t section_index;

	if (image_size < IMAGE_SECTION_TABLE_OFFSET ||
		(section_count = read_u32(image + IMAGE_SECTION_COUNT_OFFSET)) == 0 ||
		section_count > (image_size - IMAGE_SECTION_TABLE_OFFSET) / IMAGE_SECTION_ENTRY_SIZE) {
		return 0;
	}
	for (section_index = 0; section_index < section_count; ++section_index) {
		size_t entry = IMAGE_SECTION_TABLE_OFFSET + section_index * IMAGE_SECTION_ENTRY_SIZE;
		uint64_t section_offset;
		uint64_t section_size;
		Reader reader;
		uint32_t minimal_struct_count;
		uint32_t frame_count;
		uint32_t frame_index;

		if (read_u32(image + entry + IMAGE_SECTION_KIND_OFFSET) !=
			PO_PROGRAM_IMAGE_SECTION_DEBUG_LOCALS) {
			continue;
		}
		section_offset = read_u64(image + entry + IMAGE_SECTION_DATA_OFFSET);
		section_size = read_u64(image + entry + IMAGE_SECTION_SIZE_OFFSET);
		if (section_offset > SIZE_MAX || section_size > SIZE_MAX || section_offset > image_size ||
			section_size > image_size - (size_t)section_offset) {
			return 0;
		}
		reader.data = image + (size_t)section_offset;
		reader.size = (size_t)section_size;
		reader.offset = 0;
		if (!reader_u32(&reader, &minimal_struct_count) || !reader_u32(&reader, &frame_count)) {
			return 0;
		}
		(void)minimal_struct_count;
		for (frame_index = 0; frame_index < frame_count; ++frame_index) {
			uint32_t local_count;
			uint32_t local_index;
			if (!reader_u32(&reader, &local_count)) {
				return 0;
			}
			for (local_index = 0; local_index < local_count; ++local_index) {
				uint32_t name_length;
				uint32_t present;
				uint32_t component_count;
				if (!reader_u32(&reader, &name_length) || !reader_skip(&reader, name_length) ||
					!reader_skip(&reader, 8 + 4 + 4 + 8 + 8 + 4) ||
					!reader_u32(&reader, &present) || present != 1 ||
					!reader_u32(&reader, &component_count) || component_count == 0 ||
					!reader_skip(&reader, 4 + 4) || reader.offset > reader.size ||
					reader.size - reader.offset < 4) {
					return 0;
				}
				*out_component_offset = (size_t)section_offset + reader.offset;
				return 1;
			}
		}
		return 0;
	}
	return 0;
}

int main(void)
{
	const char* source_path = POCO_PORTABLE_DEBUG_SOURCE;
	PortableBinary binary;
	PortableBinary reserialized;
	PocoVm* vm = NULL;
	PocoProgram* compiled = NULL;
	PocoProgram* decoded = NULL;
	PocoProgram* rejected = NULL;
	uint8_t* malformed_image = NULL;
	size_t component_offset = SIZE_MAX;
	int32_t compiled_result = 0;
	int32_t decoded_result = 0;
	int compared_lines = 0;
	int compared_locals = 0;
	int compared_struct_local = 0;
	int ok = 1;

	memset(&binary, 0, sizeof(binary));
	memset(&reserialized, 0, sizeof(reserialized));
	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "VM creation failed");
	ok &= check(poco_vm_compile_file(vm, source_path, &compiled) == POCO_STATUS_OK,
				"fixture compilation failed");
	if (compiled == NULL) {
		goto cleanup;
	}
	ok &= check(serialize_extended(compiled, &binary), "extended serialization failed");
	if (binary.bytes == NULL) {
		goto cleanup;
	}
	ok &=
		check(poco_vm_deserialize_buffer(vm, binary.bytes, binary.size, &decoded) == POCO_STATUS_OK,
			  "extended binary did not load through the public portable path");
	if (decoded == NULL) {
		goto cleanup;
	}
	ok &= check(decoded->debug_level == POCO_DEBUG_LEVEL_EXTENDED,
				"decoded binary lost its extended debug level");
	ok &= check(
		strcmp(compiled->source_name, decoded->source_name) == 0 && compiled->source_path != NULL &&
			decoded->source_path != NULL &&
			strcmp(compiled->source_path, decoded->source_path) == 0 &&
			memcmp(compiled->source_hash, decoded->source_hash, sizeof(compiled->source_hash)) == 0,
		"source name, path, or hash changed during portable load");
	ok &= check(same_debug_frames(compiled->code.functions, decoded->code.functions,
								  &compared_lines, &compared_locals, &compared_struct_local),
				"opcode-line map or debug-local fields changed during portable load");
	ok &= check(compared_lines > 0, "fixture did not exercise opcode-line metadata");
	ok &= check(compared_locals > 0, "fixture did not exercise debug-local metadata");
	ok &= check(compared_struct_local,
				"fixture did not exercise portable struct reconstruction for a debug local");
	ok &= check(serialize_extended(decoded, &reserialized),
				"decoded binary could not be serialized again");
	ok &= check(binary.size == reserialized.size &&
					memcmp(binary.bytes, reserialized.bytes, binary.size) == 0,
				"decoded extended debug fields were not byte-identical when reserialized");
	ok &= check(poco_vm_run(vm, compiled, NULL, &compiled_result) == POCO_STATUS_OK &&
					poco_vm_run(vm, decoded, NULL, &decoded_result) == POCO_STATUS_OK &&
					compiled_result == 42 && decoded_result == compiled_result,
				"portable debug binary changed program execution");

	malformed_image = (uint8_t*)malloc(binary.image_size);
	ok &= check(malformed_image != NULL, "malformed-image allocation failed");
	if (malformed_image != NULL) {
		memcpy(malformed_image, binary.bytes + binary.image_offset, binary.image_size);
		ok &= check(
			locate_debug_type_component(malformed_image, binary.image_size, &component_offset),
			"debug-local type component could not be located");
		if (component_offset <= binary.image_size - 4) {
			write_u32(malformed_image + component_offset, UINT32_MAX);
			ok &= check(po_program_image_decode(vm, malformed_image, binary.image_size,
												POCO_DEBUG_LEVEL_EXTENDED, NULL,
												&rejected) == PO_PROGRAM_IMAGE_MALFORMED,
						"unknown debug-local type was not a hard load error");
			ok &= check(rejected == NULL, "rejected debug image returned a program");
		}
	}

cleanup:
	free(malformed_image);
	free(reserialized.bytes);
	free(binary.bytes);
	poco_program_destroy(rejected);
	poco_program_destroy(decoded);
	poco_program_destroy(compiled);
	poco_vm_destroy(vm);
	if (!ok) {
		return 1;
	}
	printf("portable debug metadata passed\n");
	return 0;
}
