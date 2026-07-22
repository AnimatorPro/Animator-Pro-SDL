#include "program_image.h"

#include "poco_endian.h"
#include "program_internal.h"
#include "pocoload.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define PO_IMAGE_VERSION_MULTI_SOURCE 3u
#define PO_IMAGE_MINIMAL_SECTION_COUNT 5u
#define PO_IMAGE_EXTENDED_SECTION_COUNT 6u
#define PO_IMAGE_MAX_SECTION_COUNT (PO_IMAGE_EXTENDED_SECTION_COUNT + 1u)
#define PO_IMAGE_HEADER_SIZE(section_count) (40u + (section_count) * 24u)
#define PO_IMAGE_NO_INDEX UINT32_MAX

static const uint8_t po_image_magic[8] = {'P', 'O', 'I', 'M', 'G', '0', '0', '1'};

typedef struct PoWriter {
	uint8_t* data;
	size_t size;
	size_t capacity;
} PoWriter;

typedef struct PoReader {
	const uint8_t* data;
	size_t size;
	size_t offset;
} PoReader;

typedef struct PoFrameSet {
	const Func_frame** items;
	size_t count;
	size_t capacity;
} PoFrameSet;

typedef struct PoStructSet {
	const Struct_info** items;
	size_t count;
	size_t capacity;
} PoStructSet;

typedef struct PoDecodedStructs {
	Struct_info** items;
	uint32_t* member_counts;
	size_t count;
	PoReader members;
} PoDecodedStructs;

typedef struct PoSectionView {
	const uint8_t* data;
	size_t size;
} PoSectionView;

static int po_size_add(size_t left, size_t right, size_t* out)
{
	if (left > SIZE_MAX - right) {
		return 0;
	}
	*out = left + right;
	return 1;
}

static int po_writer_reserve(PoWriter* writer, size_t extra)
{
	size_t required;
	size_t capacity;
	uint8_t* data;

	if (!po_size_add(writer->size, extra, &required)) {
		return 0;
	}
	if (required <= writer->capacity) {
		return 1;
	}
	capacity = writer->capacity != 0 ? writer->capacity : 256;
	while (capacity < required) {
		if (capacity > SIZE_MAX / 2) {
			capacity = required;
			break;
		}
		capacity *= 2;
	}
	data = realloc(writer->data, capacity);
	if (data == NULL) {
		return 0;
	}
	writer->data = data;
	writer->capacity = capacity;
	return 1;
}

static int po_writer_bytes(PoWriter* writer, const void* data, size_t size)
{
	if (!po_writer_reserve(writer, size)) {
		return 0;
	}
	if (size != 0) {
		memcpy(writer->data + writer->size, data, size);
	}
	writer->size += size;
	return 1;
}

static int po_writer_u32(PoWriter* writer, uint32_t value)
{
	uint8_t bytes[4];
	po_store_le32(bytes, value);
	return po_writer_bytes(writer, bytes, sizeof(bytes));
}

static int po_writer_u64(PoWriter* writer, uint64_t value)
{
	uint8_t bytes[8];
	po_store_le64(bytes, value);
	return po_writer_bytes(writer, bytes, sizeof(bytes));
}

static int po_writer_string(PoWriter* writer, const char* text)
{
	size_t length = text != NULL ? strlen(text) : 0;
	return length <= UINT32_MAX && po_writer_u32(writer, (uint32_t)length) &&
		   po_writer_bytes(writer, text, length);
}

static int po_reader_bytes(PoReader* reader, void* out, size_t size)
{
	if (reader->offset > reader->size || size > reader->size - reader->offset) {
		return 0;
	}
	if (size != 0 && out != NULL) {
		memcpy(out, reader->data + reader->offset, size);
	}
	reader->offset += size;
	return 1;
}

static int po_reader_u32(PoReader* reader, uint32_t* out)
{
	uint8_t bytes[4];
	if (!po_reader_bytes(reader, bytes, sizeof(bytes))) {
		return 0;
	}
	*out = po_load_le32(bytes);
	return 1;
}

static int po_reader_u64(PoReader* reader, uint64_t* out)
{
	uint8_t bytes[8];
	if (!po_reader_bytes(reader, bytes, sizeof(bytes))) {
		return 0;
	}
	*out = po_load_le64(bytes);
	return 1;
}

static char* po_reader_arena_string(PoReader* reader, Poco_cb* owner)
{
	uint32_t length;
	char* text;

	if (!po_reader_u32(reader, &length) || reader->offset > reader->size ||
		length > reader->size - reader->offset) {
		return NULL;
	}
	text = po_memzalloc(owner, (size_t)length + 1);
	if (text == NULL) {
		return NULL;
	}
	if (!po_reader_bytes(reader, text, length)) {
		return NULL;
	}
	return text;
}

static int po_frames_add(PoFrameSet* frames, const Func_frame* frame)
{
	size_t index;
	const Func_frame** items;

	if (frame == NULL) {
		return 1;
	}
	for (index = 0; index < frames->count; ++index) {
		if (frames->items[index] == frame) {
			return 1;
		}
	}
	if (frames->count == frames->capacity) {
		size_t capacity = frames->capacity != 0 ? frames->capacity * 2 : 32;
		items = realloc(frames->items, capacity * sizeof(*items));
		if (items == NULL) {
			return 0;
		}
		frames->items = items;
		frames->capacity = capacity;
	}
	frames->items[frames->count++] = frame;
	return po_frames_add(frames, frame->next) && po_frames_add(frames, frame->mlink);
}

static uint32_t po_frame_index(const PoFrameSet* frames, const Func_frame* frame)
{
	size_t index;
	if (frame == NULL) {
		return PO_IMAGE_NO_INDEX;
	}
	for (index = 0; index < frames->count; ++index) {
		if (frames->items[index] == frame) {
			return (uint32_t)index;
		}
	}
	return PO_IMAGE_NO_INDEX;
}

static uint32_t po_frame_code_index(const PoFrameSet* frames, const void* code)
{
	size_t index;
	for (index = 0; index < frames->count; ++index) {
		if (frames->items[index]->type == CFF_C && frames->items[index]->code_pt == code) {
			return (uint32_t)index;
		}
	}
	return PO_IMAGE_NO_INDEX;
}

static uint32_t po_struct_index(const PoStructSet* structs, const Struct_info* struct_info)
{
	size_t index;

	if (struct_info == NULL) {
		return PO_IMAGE_NO_INDEX;
	}
	for (index = 0; index < structs->count; ++index) {
		if (structs->items[index] == struct_info) {
			return (uint32_t)index;
		}
	}
	return PO_IMAGE_NO_INDEX;
}

static PoProgramImageStatus po_structs_add(PoStructSet* structs, const Struct_info* struct_info)
{
	const Symbol* member;
	size_t index;
	const Struct_info** items;

	if (struct_info == NULL ||
		(struct_info->type != TYPE_STRUCT && struct_info->type != TYPE_UNION)) {
		return PO_PROGRAM_IMAGE_OK;
	}
	for (index = 0; index < structs->count; ++index) {
		if (structs->items[index] == struct_info) {
			return PO_PROGRAM_IMAGE_OK;
		}
	}
	if (structs->count == UINT32_MAX) {
		return PO_PROGRAM_IMAGE_UNSUPPORTED;
	}
	if (structs->count == structs->capacity) {
		size_t capacity = structs->capacity != 0 ? structs->capacity * 2 : 16;
		items = realloc(structs->items, capacity * sizeof(*items));
		if (items == NULL) {
			return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		}
		structs->items = items;
		structs->capacity = capacity;
	}
	structs->items[structs->count++] = struct_info;
	for (member = struct_info->elements; member != NULL; member = member->next) {
		uint32_t component;
		if (member->ti == NULL) {
			continue;
		}
		for (component = 0; component < member->ti->comp_count; ++component) {
			PoProgramImageStatus status;
			if (member->ti->comp[component] != TYPE_STRUCT) {
				continue;
			}
			status = po_structs_add(structs, member->ti->sdims[component].pt);
			if (status != PO_PROGRAM_IMAGE_OK) {
				return status;
			}
		}
	}
	return PO_PROGRAM_IMAGE_OK;
}

static PoProgramImageStatus po_collect_type_structs(PoStructSet* structs, const Type_info* type)
{
	uint32_t component;

	if (type == NULL) {
		return PO_PROGRAM_IMAGE_OK;
	}
	for (component = 0; component < type->comp_count; ++component) {
		PoProgramImageStatus status;
		if (type->comp[component] != TYPE_STRUCT) {
			continue;
		}
		status = po_structs_add(structs, type->sdims[component].pt);
		if (status != PO_PROGRAM_IMAGE_OK) {
			return status;
		}
	}
	return PO_PROGRAM_IMAGE_OK;
}

static PoProgramImageStatus po_collect_structs(const PocoProgram* program, const PoFrameSet* frames,
											   PocoDebugLevel debug_level, PoStructSet* structs,
											   size_t* out_minimal_struct_count)
{
	const Poco_run_env* executable = program->executable;
	const Struct_info* struct_info;
	size_t frame_index;
	size_t remaining =
		program->minimal_struct_count_known ? program->minimal_struct_count : SIZE_MAX;

	for (struct_info = executable->struct_infos; struct_info != NULL;
		 struct_info = struct_info->next) {
		PoProgramImageStatus status;
		if (remaining == 0) {
			break;
		}
		status = po_structs_add(structs, struct_info);
		if (status != PO_PROGRAM_IMAGE_OK) {
			return status;
		}
		if (remaining != SIZE_MAX) {
			--remaining;
		}
	}
	if (remaining != 0 && remaining != SIZE_MAX) {
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	*out_minimal_struct_count = structs->count;
	if (debug_level == POCO_DEBUG_LEVEL_EXTENDED) {
		for (frame_index = 0; frame_index < frames->count; ++frame_index) {
			const PocoDebugLocal* local;
			for (local = frames->items[frame_index]->debug_locals; local != NULL;
				 local = local->next) {
				PoProgramImageStatus status = po_collect_type_structs(structs, local->type);
				if (status != PO_PROGRAM_IMAGE_OK) {
					return status;
				}
			}
		}
	}
	return PO_PROGRAM_IMAGE_OK;
}

static size_t po_literal_count(const Names* literal)
{
	size_t count = 0;
	for (; literal != NULL; literal = literal->next) {
		++count;
	}
	return count;
}

static const Names* po_literal_at(const Names* literal, uint32_t wanted)
{
	uint32_t index = 0;
	for (; literal != NULL; literal = literal->next, ++index) {
		if (index == wanted) {
			return literal;
		}
	}
	return NULL;
}

static int po_literal_pointer(const Names* literal, const void* pointer, uint32_t* out_index,
							  uint64_t* out_offset)
{
	uint32_t index = 0;
	uintptr_t address = (uintptr_t)pointer;

	for (; literal != NULL; literal = literal->next, ++index) {
		uintptr_t start = (uintptr_t)literal->name;
		size_t length = strlen(literal->name);
		if (address >= start && address <= start + length) {
			*out_index = index;
			*out_offset = (uint64_t)(address - start);
			return 1;
		}
	}
	return 0;
}

static PoProgramImageStatus po_encode_type(PoWriter* writer, const Type_info* type,
										   const PoFrameSet* frames, const PoStructSet* structs)
{
	uint32_t index;

	if (!po_writer_u32(writer, type != NULL ? 1 : 0)) {
		return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
	}
	if (type == NULL) {
		return PO_PROGRAM_IMAGE_OK;
	}
	if (type->comp_count > MAX_TYPE_COMPS || !po_writer_u32(writer, type->comp_count) ||
		!po_writer_u32(writer, (uint32_t)type->flags) ||
		!po_writer_u32(writer, (uint32_t)type->ido_type)) {
		return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
	}
	for (index = 0; index < type->comp_count; ++index) {
		uint32_t comp = (uint32_t)type->comp[index];
		uint32_t reference;

		if (!po_writer_u32(writer, comp)) {
			return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		}
		if (comp == TYPE_STRUCT) {
			reference = po_struct_index(structs, type->sdims[index].pt);
			if (reference == PO_IMAGE_NO_INDEX) {
				return PO_PROGRAM_IMAGE_UNSUPPORTED;
			}
			if (!po_writer_u32(writer, 2) || !po_writer_u32(writer, reference)) {
				return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
			}
		} else if (comp == TYPE_FUNCTION && type->sdims[index].pt != NULL) {
			reference = po_frame_index(frames, type->sdims[index].pt);
			if (reference == PO_IMAGE_NO_INDEX) {
				return PO_PROGRAM_IMAGE_UNSUPPORTED;
			}
			if (!po_writer_u32(writer, 1) || !po_writer_u32(writer, reference)) {
				return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
			}
		} else {
			if (!po_writer_u32(writer, 0) ||
				!po_writer_u64(writer, (uint64_t)(int64_t)type->sdims[index].l)) {
				return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
			}
		}
	}
	return PO_PROGRAM_IMAGE_OK;
}

static PoProgramImageStatus po_decode_type(PoReader* reader, Poco_cb* owner, Func_frame** frames,
										   size_t frame_count, Struct_info** structs,
										   size_t struct_count, Type_info** out_type)
{
	uint32_t present;
	uint32_t count;
	uint32_t flags;
	uint32_t ido;
	uint32_t index;
	Type_info* type;
	size_t allocation_size;

	*out_type = NULL;
	if (!po_reader_u32(reader, &present)) {
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	if (present == 0) {
		return PO_PROGRAM_IMAGE_OK;
	}
	if (present != 1 || !po_reader_u32(reader, &count) || !po_reader_u32(reader, &flags) ||
		!po_reader_u32(reader, &ido) || count > MAX_TYPE_COMPS ||
		(ido != UINT32_MAX && ido >= IDO_LAST)) {
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	allocation_size =
		sizeof(*type) + (size_t)count * sizeof(TypeComp) + (size_t)count * sizeof(Pt_long);
	type = po_memzalloc(owner, allocation_size);
	if (type == NULL) {
		return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
	}
	type->comp = (TypeComp*)(type + 1);
	type->sdims = (Pt_long*)(type->comp + count);
	type->comp_alloc = (UBYTE)count;
	type->comp_count = (UBYTE)count;
	type->flags = (TypeFlags)flags;
	type->ido_type = (IdoType)(int32_t)ido;
	for (index = 0; index < count; ++index) {
		uint32_t comp;
		uint32_t kind;
		uint64_t value;

		if (!po_reader_u32(reader, &comp) || !po_reader_u32(reader, &kind) || comp >= TYPE_BAD ||
			comp == TYPE_UNION) {
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		type->comp[index] = (TypeComp)comp;
		if (kind == 0 && comp != TYPE_STRUCT) {
			if (!po_reader_u64(reader, &value)) {
				return PO_PROGRAM_IMAGE_MALFORMED;
			}
			type->sdims[index].l = (long)(int64_t)value;
			if ((int64_t)type->sdims[index].l != (int64_t)value) {
				return PO_PROGRAM_IMAGE_UNSUPPORTED;
			}
		} else if (kind == 1 && comp == TYPE_FUNCTION) {
			uint32_t reference;
			if (!po_reader_u32(reader, &reference) || reference >= frame_count) {
				return PO_PROGRAM_IMAGE_MALFORMED;
			}
			type->sdims[index].pt = frames[reference];
		} else if (kind == 2 && comp == TYPE_STRUCT) {
			uint32_t reference;
			if (!po_reader_u32(reader, &reference) || reference >= struct_count) {
				return PO_PROGRAM_IMAGE_MALFORMED;
			}
			type->sdims[index].pt = structs[reference];
		} else {
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
	}
	*out_type = type;
	return PO_PROGRAM_IMAGE_OK;
}

static PoProgramImageStatus po_encode_code_frame(PoWriter* writer, const Func_frame* frame,
												 const PoFrameSet* frames, const Names* literals)
{
	const uint8_t* cursor = (const uint8_t*)frame->code_pt;
	const uint8_t* end = cursor + frame->code_size;

	while (cursor < end) {
		int op;
		const Poco_op_table* entry;
		const uint8_t* operand;

		if ((size_t)(end - cursor) < sizeof(op)) {
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		memcpy(&op, cursor, sizeof(op));
		cursor += sizeof(op);
		if (op < 0 || op >= po_ins_table_els) {
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		entry = &po_ins_table[op];
		if ((size_t)(end - cursor) < (size_t)entry->op_size) {
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		operand = cursor;
		if (!po_writer_u32(writer, (uint32_t)op)) {
			return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		}
		switch (entry->op_ext) {
			case OEX_NONE:
				break;
			case OEX_VAR:
			case OEX_INT:
			case OEX_LABEL: {
				int value;
				memcpy(&value, operand, sizeof(value));
				if (!po_writer_u32(writer, (uint32_t)value)) {
					return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
				}
				break;
			}
			case OEX_LONG: {
				long value;
				memcpy(&value, operand, sizeof(value));
				if (!po_writer_u64(writer, (uint64_t)(int64_t)value)) {
					return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
				}
				break;
			}
			case OEX_DOUBLE: {
				double value;
				uint64_t bits;
				memcpy(&value, operand, sizeof(value));
				memcpy(&bits, &value, sizeof(bits));
				if (!po_writer_u64(writer, bits)) {
					return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
				}
				break;
			}
			case OEX_ADDRESS: {
				int offset;
				long span;
				memcpy(&offset, operand, sizeof(offset));
				memcpy(&span, operand + sizeof(offset), sizeof(span));
				if (!po_writer_u32(writer, (uint32_t)offset) ||
					!po_writer_u64(writer, (uint64_t)(int64_t)span)) {
					return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
				}
				break;
			}
			case OEX_FUNCTION: {
				void* pointer;
				uint32_t reference;
				memcpy(&pointer, operand, sizeof(pointer));
				reference = po_frame_index(frames, pointer);
				if (reference == PO_IMAGE_NO_INDEX) {
					return PO_PROGRAM_IMAGE_UNSUPPORTED;
				}
				if (!po_writer_u32(writer, reference)) {
					return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
				}
				break;
			}
			case OEX_CFUNCTION: {
				void* pointer;
				uint32_t reference;
				memcpy(&pointer, operand, sizeof(pointer));
				reference = po_frame_code_index(frames, pointer);
				if (reference == PO_IMAGE_NO_INDEX) {
					return PO_PROGRAM_IMAGE_UNKNOWN_BINDING;
				}
				if (!po_writer_u32(writer, reference)) {
					return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
				}
				break;
			}
			case OEX_POINTER: {
				Popot pointer;
				uint32_t literal_index;
				uint32_t current_index;
				uint64_t minimum;
				uint64_t maximum;
				uint64_t current;

				memcpy(&pointer, operand, sizeof(pointer));
				if (pointer.pt == NULL && pointer.max == NULL) {
					if (!po_writer_u32(writer, 0)) {
						return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
					}
					break;
				}
				if (!po_literal_pointer(literals, pointer.pt, &current_index, &current) ||
					!po_literal_pointer(literals, pointer.min, &literal_index, &minimum)) {
					return PO_PROGRAM_IMAGE_UNSUPPORTED;
				}
				{
					uint32_t maximum_index;
					if (!po_literal_pointer(literals, pointer.max, &maximum_index, &maximum) ||
						maximum_index != literal_index || current_index != literal_index ||
						minimum > current || current > maximum) {
						return PO_PROGRAM_IMAGE_UNSUPPORTED;
					}
				}
				if (!po_writer_u32(writer, 1) || !po_writer_u32(writer, literal_index) ||
					!po_writer_u64(writer, minimum) || !po_writer_u64(writer, maximum) ||
					!po_writer_u64(writer, current)) {
					return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
				}
				break;
			}
			default:
				return PO_PROGRAM_IMAGE_UNSUPPORTED;
		}
		cursor += entry->op_size;
	}
	return cursor == end ? PO_PROGRAM_IMAGE_OK : PO_PROGRAM_IMAGE_MALFORMED;
}

static PoProgramImageStatus po_encode_code(PoWriter* section, const PoFrameSet* frames,
										   const Names* literals)
{
	size_t index;

	if (frames->count > UINT32_MAX || !po_writer_u32(section, (uint32_t)frames->count)) {
		return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
	}
	for (index = 0; index < frames->count; ++index) {
		PoWriter encoded = {0};
		PoProgramImageStatus status =
			po_encode_code_frame(&encoded, frames->items[index], frames, literals);
		if (status != PO_PROGRAM_IMAGE_OK) {
			free(encoded.data);
			return status;
		}
		if (!po_writer_u64(section, encoded.size) ||
			!po_writer_bytes(section, encoded.data, encoded.size)) {
			free(encoded.data);
			return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		}
		free(encoded.data);
	}
	return PO_PROGRAM_IMAGE_OK;
}

static PoProgramImageStatus po_encode_constants(PoWriter* section, const Names* literal)
{
	size_t count = po_literal_count(literal);
	if (count > UINT32_MAX || !po_writer_u32(section, (uint32_t)count)) {
		return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
	}
	for (; literal != NULL; literal = literal->next) {
		if (!po_writer_string(section, literal->name)) {
			return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		}
	}
	return PO_PROGRAM_IMAGE_OK;
}

static PoProgramImageStatus po_native_offset_to_instruction(const Func_frame* frame, long offset,
															uint64_t* out_instruction);
static PoProgramImageStatus po_instruction_to_native_offset(const Func_frame* frame,
															uint64_t instruction, long* out_offset);

static PoProgramImageStatus po_encode_prototypes(PoWriter* section, const PoFrameSet* frames,
												 const PoStructSet* structs)
{
	size_t index;

	if (!po_writer_u32(section, (uint32_t)frames->count)) {
		return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
	}
	for (index = 0; index < frames->count; ++index) {
		const Func_frame* frame = frames->items[index];
		uint32_t line_count =
			frame->ld != NULL && frame->ld->count > 0 ? (uint32_t)frame->ld->count : 0;
		uint32_t line;
		PoProgramImageStatus status;

		if (frame->unit_index > UINT32_MAX || !po_writer_string(section, frame->name) ||
			!po_writer_u32(section, (uint32_t)(int32_t)frame->pcount) ||
			!po_writer_u32(section, (uint32_t)(int32_t)frame->type) ||
			!po_writer_u32(section, frame->binding_flags) ||
			!po_writer_u64(section, (uint64_t)(int64_t)frame->magic) ||
			!po_writer_u32(section, frame->got_code != 0) ||
			!po_writer_u32(section, po_frame_index(frames, frame->next)) ||
			!po_writer_u32(section, po_frame_index(frames, frame->mlink)) ||
			!po_writer_u32(section, po_frame_index(frames, frame->compiled_frame)) ||
			!po_writer_u32(section, (uint32_t)frame->unit_index)) {
			return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		}
		status = po_encode_type(section, frame->return_type, frames, structs);
		if (status != PO_PROGRAM_IMAGE_OK) {
			return status;
		}
		if (!po_writer_u32(section, line_count)) {
			return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		}
		/* Portable opcode<->line map: store the code position as an instruction
		 * ordinal rather than a native byte offset, matching the debug-locals
		 * live ranges (see po_encode_debug_locals). Native offsets are not
		 * stable across ABIs because operand byte sizes differ per target;
		 * ordinals are. Decode reconstructs native offsets in
		 * po_relocate_line_offsets once the native code has been rebuilt. */
		for (line = 0; line < line_count; ++line) {
			uint64_t ordinal;
			PoProgramImageStatus map_status =
				po_native_offset_to_instruction(frame, frame->ld->offsets[line], &ordinal);
			if (map_status != PO_PROGRAM_IMAGE_OK) {
				return map_status;
			}
			if (!po_writer_u64(section, ordinal) ||
				!po_writer_u32(section, (uint32_t)frame->unit_index) ||
				!po_writer_u64(section, (uint64_t)(int64_t)frame->ld->lines[line])) {
				return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
			}
		}
	}
	return PO_PROGRAM_IMAGE_OK;
}

static PoProgramImageStatus po_encode_symbols(PoWriter* section, const PoFrameSet* frames,
											  const PoStructSet* structs)
{
	size_t index;

	if (!po_writer_u32(section, (uint32_t)frames->count)) {
		return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
	}
	for (index = 0; index < frames->count; ++index) {
		const Symbol* symbol;
		uint32_t count = 0;
		PoProgramImageStatus status;

		for (symbol = frames->items[index]->parameters; symbol != NULL; symbol = symbol->link) {
			++count;
		}
		if (!po_writer_u32(section, count)) {
			return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		}
		for (symbol = frames->items[index]->parameters; symbol != NULL; symbol = symbol->link) {
			if (!po_writer_string(section, symbol->name) ||
				!po_writer_u64(section, (uint64_t)(int64_t)symbol->symval.l) ||
				!po_writer_u32(section, (uint32_t)symbol->tok_type) ||
				!po_writer_u32(section, (uint32_t)(int32_t)symbol->scope) ||
				!po_writer_u32(section, (uint32_t)symbol->storage_scope) ||
				!po_writer_u32(section, (uint32_t)symbol->flags)) {
				return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
			}
			status = po_encode_type(section, symbol->ti, frames, structs);
			if (status != PO_PROGRAM_IMAGE_OK) {
				return status;
			}
		}
	}
	return PO_PROGRAM_IMAGE_OK;
}

static PoProgramImageStatus po_native_offset_to_instruction(const Func_frame* frame, long offset,
															uint64_t* out_instruction)
{
	const uint8_t* cursor = (const uint8_t*)frame->code_pt;
	const uint8_t* end = cursor + frame->code_size;
	uint64_t instruction = 0;

	if (offset < 0 || offset > frame->code_size) {
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	while (cursor < end) {
		int op;
		const Poco_op_table* entry;
		if ((long)(cursor - (const uint8_t*)frame->code_pt) == offset) {
			*out_instruction = instruction;
			return PO_PROGRAM_IMAGE_OK;
		}
		if ((size_t)(end - cursor) < sizeof(op)) {
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		memcpy(&op, cursor, sizeof(op));
		cursor += sizeof(op);
		if (op < 0 || op >= po_ins_table_els) {
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		entry = &po_ins_table[op];
		if ((size_t)(end - cursor) < (size_t)entry->op_size) {
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		cursor += entry->op_size;
		++instruction;
	}
	if (offset == frame->code_size) {
		*out_instruction = instruction;
		return PO_PROGRAM_IMAGE_OK;
	}
	return PO_PROGRAM_IMAGE_MALFORMED;
}

static PoProgramImageStatus po_instruction_to_native_offset(const Func_frame* frame,
															uint64_t instruction, long* out_offset)
{
	const uint8_t* cursor = (const uint8_t*)frame->code_pt;
	const uint8_t* start = cursor;
	const uint8_t* end = cursor + frame->code_size;
	uint64_t current = 0;

	while (current < instruction && cursor < end) {
		int op;
		const Poco_op_table* entry;
		if ((size_t)(end - cursor) < sizeof(op)) {
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		memcpy(&op, cursor, sizeof(op));
		cursor += sizeof(op);
		if (op < 0 || op >= po_ins_table_els) {
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		entry = &po_ins_table[op];
		if ((size_t)(end - cursor) < (size_t)entry->op_size) {
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		cursor += entry->op_size;
		++current;
	}
	if (current != instruction) {
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	*out_offset = (long)(cursor - start);
	return PO_PROGRAM_IMAGE_OK;
}

static PoProgramImageStatus po_encode_debug_locals(PoWriter* section, const PoFrameSet* frames,
												   const PoStructSet* structs,
												   size_t minimal_struct_count)
{
	size_t frame_index;

	/* Live endpoints are instruction ordinals in the portable image. Decode
	 * converts them back to byte offsets after rebuilding target-native code. */
	if (minimal_struct_count > UINT32_MAX ||
		!po_writer_u32(section, (uint32_t)minimal_struct_count) ||
		!po_writer_u32(section, (uint32_t)frames->count)) {
		return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
	}
	for (frame_index = 0; frame_index < frames->count; ++frame_index) {
		const PocoDebugLocal* local;
		uint32_t count = 0;

		for (local = frames->items[frame_index]->debug_locals; local != NULL; local = local->next) {
			if (count == UINT32_MAX) {
				return PO_PROGRAM_IMAGE_UNSUPPORTED;
			}
			++count;
		}
		if (!po_writer_u32(section, count)) {
			return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		}
		for (local = frames->items[frame_index]->debug_locals; local != NULL; local = local->next) {
			uint64_t live_start;
			uint64_t live_end;
			PoProgramImageStatus status = po_native_offset_to_instruction(
				frames->items[frame_index], local->live_start, &live_start);
			if (status == PO_PROGRAM_IMAGE_OK) {
				status = po_native_offset_to_instruction(frames->items[frame_index],
														 local->live_end, &live_end);
			}
			if (status != PO_PROGRAM_IMAGE_OK) {
				return status;
			}
			if (!po_writer_string(section, local->name) ||
				!po_writer_u64(section, (uint64_t)(int64_t)local->frame_offset) ||
				!po_writer_u32(section, (uint32_t)(int32_t)local->scope) ||
				!po_writer_u32(section, (uint32_t)local->storage_scope) ||
				!po_writer_u64(section, live_start) || !po_writer_u64(section, live_end) ||
				!po_writer_u32(section, local->live_range_approximate != 0)) {
				return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
			}
			status = po_encode_type(section, local->type, frames, structs);
			if (status != PO_PROGRAM_IMAGE_OK) {
				return status;
			}
		}
	}
	return PO_PROGRAM_IMAGE_OK;
}

static PoProgramImageStatus po_encode_structs(PoWriter* section, const PoStructSet* structs,
											  const PoFrameSet* frames)
{
	size_t index;

	if (!po_writer_u32(section, (uint32_t)structs->count)) {
		return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
	}
	/* Headers are contiguous so a loader can establish every nominal identity
	 * before any Type_info is decoded. */
	for (index = 0; index < structs->count; ++index) {
		const Struct_info* struct_info = structs->items[index];
		const Symbol* member;
		uint32_t member_count = 0;

		for (member = struct_info->elements; member != NULL; member = member->next) {
			if (member_count == UINT32_MAX) {
				return PO_PROGRAM_IMAGE_UNSUPPORTED;
			}
			++member_count;
		}
		if (member_count != (uint32_t)struct_info->el_count ||
			!po_writer_u32(section, (uint32_t)struct_info->type) ||
			!po_writer_string(section, struct_info->name) ||
			!po_writer_u32(section, member_count)) {
			return member_count != (uint32_t)struct_info->el_count ? PO_PROGRAM_IMAGE_MALFORMED
																   : PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		}
	}
	for (index = 0; index < structs->count; ++index) {
		const Symbol* member;
		for (member = structs->items[index]->elements; member != NULL; member = member->next) {
			PoProgramImageStatus status;
			if (!po_writer_string(section, member->name)) {
				return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
			}
			status = po_encode_type(section, member->ti, frames, structs);
			if (status != PO_PROGRAM_IMAGE_OK) {
				return status;
			}
		}
	}
	return PO_PROGRAM_IMAGE_OK;
}

static PoProgramImageStatus po_encode_external_libraries(PoWriter* section,
														 const Poco_lib* libraries)
{
	const Poco_lib* library;
	uint32_t count = 0;

	for (library = libraries; library != NULL; library = library->next) {
		if (count == UINT32_MAX) {
			return PO_PROGRAM_IMAGE_UNSUPPORTED;
		}
		++count;
	}
	if (!po_writer_u32(section, count)) {
		return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
	}
	for (library = libraries; library != NULL; library = library->next) {
		const char* name = poco_loaded_library_requested_name(library);
		if (name == NULL || name[0] == '\0') {
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		if (!po_writer_string(section, name)) {
			return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		}
	}
	return PO_PROGRAM_IMAGE_OK;
}

static void po_write_u32_at(uint8_t* data, size_t offset, uint32_t value)
{
	po_store_le32(data + offset, value);
}

static void po_write_u64_at(uint8_t* data, size_t offset, uint64_t value)
{
	po_store_le64(data + offset, value);
}

PoProgramImageStatus po_program_image_encode(const PocoProgram* program, PocoDebugLevel debug_level,
											 uint8_t** out_image, size_t* out_image_size)
{
	PoFrameSet frames = {0};
	PoStructSet structs = {0};
	PoWriter sections[PO_IMAGE_MAX_SECTION_COUNT] = {{0}};
	PoProgramImageStatus status;
	size_t minimal_struct_count = 0;
	uint32_t base_section_count = debug_level == POCO_DEBUG_LEVEL_EXTENDED
									  ? PO_IMAGE_EXTENDED_SECTION_COUNT
									  : PO_IMAGE_MINIMAL_SECTION_COUNT;
	uint32_t section_count = base_section_count + 1u;
	uint32_t image_version = PO_IMAGE_VERSION_MULTI_SOURCE;
	size_t total = PO_IMAGE_HEADER_SIZE(section_count);
	size_t offset;
	size_t index;
	uint8_t* image;
	uint32_t kinds[PO_IMAGE_MAX_SECTION_COUNT] = {PO_PROGRAM_IMAGE_SECTION_CODE,
												  PO_PROGRAM_IMAGE_SECTION_CONSTANTS,
												  PO_PROGRAM_IMAGE_SECTION_PROTOTYPES,
												  PO_PROGRAM_IMAGE_SECTION_SYMBOLS,
												  PO_PROGRAM_IMAGE_SECTION_STRUCT_DEFINITIONS,
												  PO_PROGRAM_IMAGE_SECTION_DEBUG_LOCALS,
												  PO_PROGRAM_IMAGE_SECTION_EXTERNAL_LIBRARIES};

	if (out_image == NULL || out_image_size == NULL || program == NULL ||
		program->executable == NULL ||
		(debug_level != POCO_DEBUG_LEVEL_MINIMAL && debug_level != POCO_DEBUG_LEVEL_EXTENDED)) {
		return PO_PROGRAM_IMAGE_INVALID_ARGUMENT;
	}
	*out_image = NULL;
	*out_image_size = 0;
	if (debug_level == POCO_DEBUG_LEVEL_MINIMAL) {
		kinds[base_section_count] = PO_PROGRAM_IMAGE_SECTION_EXTERNAL_LIBRARIES;
	}
	if (!po_frames_add(&frames, program->code.prototypes) ||
		!po_frames_add(&frames, program->code.functions)) {
		status = PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		goto CLEANUP;
	}
	if (frames.count == 0 || frames.count > UINT32_MAX) {
		status = PO_PROGRAM_IMAGE_MALFORMED;
		goto CLEANUP;
	}
	status = po_collect_structs(program, &frames, debug_level, &structs, &minimal_struct_count);
	if (status != PO_PROGRAM_IMAGE_OK) {
		goto CLEANUP;
	}
	status = po_encode_code(&sections[0], &frames, program->code.literals);
	if (status != PO_PROGRAM_IMAGE_OK) {
		goto CLEANUP;
	}
	status = po_encode_constants(&sections[1], program->code.literals);
	if (status != PO_PROGRAM_IMAGE_OK) {
		goto CLEANUP;
	}
	status = po_encode_prototypes(&sections[2], &frames, &structs);
	if (status != PO_PROGRAM_IMAGE_OK) {
		goto CLEANUP;
	}
	status = po_encode_symbols(&sections[3], &frames, &structs);
	if (status != PO_PROGRAM_IMAGE_OK) {
		goto CLEANUP;
	}
	status = po_encode_structs(&sections[4], &structs, &frames);
	if (status != PO_PROGRAM_IMAGE_OK) {
		goto CLEANUP;
	}
	if (debug_level == POCO_DEBUG_LEVEL_EXTENDED) {
		status = po_encode_debug_locals(&sections[5], &frames, &structs, minimal_struct_count);
		if (status != PO_PROGRAM_IMAGE_OK) {
			goto CLEANUP;
		}
	}
	status =
		po_encode_external_libraries(&sections[base_section_count], program->code.loaded_libraries);
	if (status != PO_PROGRAM_IMAGE_OK) {
		goto CLEANUP;
	}
	for (index = 0; index < section_count; ++index) {
		if (!po_size_add(total, sections[index].size, &total)) {
			status = PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
			goto CLEANUP;
		}
	}
	image = calloc(1, total);
	if (image == NULL) {
		status = PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		goto CLEANUP;
	}
	memcpy(image, po_image_magic, sizeof(po_image_magic));
	po_write_u32_at(image, 8, image_version);
	po_write_u32_at(image, 12, section_count);
	po_write_u64_at(image, 16, (uint64_t)(int64_t)program->code.stack_size);
	po_write_u64_at(image, 24, (uint64_t)(int64_t)program->code.data_size);
	po_write_u32_at(image, 32, po_frame_index(&frames, program->code.functions));
	po_write_u32_at(image, 36, po_frame_index(&frames, program->code.prototypes));
	offset = PO_IMAGE_HEADER_SIZE(section_count);
	for (index = 0; index < section_count; ++index) {
		size_t descriptor = 40 + index * 24;
		po_write_u32_at(image, descriptor, kinds[index]);
		po_write_u64_at(image, descriptor + 8, offset);
		po_write_u64_at(image, descriptor + 16, sections[index].size);
		memcpy(image + offset, sections[index].data, sections[index].size);
		offset += sections[index].size;
	}
	*out_image = image;
	*out_image_size = total;
	status = PO_PROGRAM_IMAGE_OK;

CLEANUP:
	for (index = 0; index < PO_IMAGE_MAX_SECTION_COUNT; ++index) {
		free(sections[index].data);
	}
	free(frames.items);
	free(structs.items);
	return status;
}

static PoProgramImageStatus po_decode_constants(PoSectionView section, Poco_cb* owner,
												Names** out_literals)
{
	PoReader reader = {section.data, section.size, 0};
	uint32_t count;
	uint32_t index;
	Names* first = NULL;
	Names* tail = NULL;

	if (!po_reader_u32(&reader, &count)) {
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	for (index = 0; index < count; ++index) {
		char* text = po_reader_arena_string(&reader, owner);
		Names* literal;
		if (text == NULL) {
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		literal = po_memzalloc(owner, sizeof(*literal));
		if (literal == NULL) {
			return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		}
		literal->name = text;
		if (tail != NULL) {
			tail->next = literal;
		} else {
			first = literal;
		}
		tail = literal;
	}
	if (reader.offset != reader.size) {
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	*out_literals = first;
	return PO_PROGRAM_IMAGE_OK;
}

static PoProgramImageStatus po_decode_external_libraries(PoSectionView section, Poco_cb* owner,
														 Poco_lib** out_libraries)
{
	PoReader reader = {section.data, section.size, 0};
	Poco_lib* first = NULL;
	Poco_lib* tail = NULL;
	uint32_t count;
	uint32_t index;

	if (!po_reader_u32(&reader, &count) ||
		count > (reader.size - reader.offset) / (sizeof(uint32_t) + 1u)) {
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	for (index = 0; index < count; ++index) {
		Poco_lib* library = po_memzalloc(owner, sizeof(*library));
		if (library == NULL) {
			return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		}
		library->name = po_reader_arena_string(&reader, owner);
		if (library->name == NULL || library->name[0] == '\0') {
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		if (tail != NULL) {
			tail->next = library;
		} else {
			first = library;
		}
		tail = library;
	}
	if (reader.offset != reader.size) {
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	*out_libraries = first;
	return PO_PROGRAM_IMAGE_OK;
}

static PoProgramImageStatus po_external_library_status(Errcode status)
{
	switch (status) {
		case Err_no_memory:
			return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		case Err_poco_lib_not_found:
			return PO_PROGRAM_IMAGE_MODULE_NOT_FOUND;
		case Err_poco_lib_no_entry:
			return PO_PROGRAM_IMAGE_MODULE_NO_ENTRY;
		case Err_poco_lib_invalid:
			return PO_PROGRAM_IMAGE_MODULE_INVALID;
		case Err_poco_lib_version:
			return PO_PROGRAM_IMAGE_MODULE_VERSION;
		case Err_poco_lib_empty:
			return PO_PROGRAM_IMAGE_MODULE_EMPTY;
		case Err_poco_lib_load_failed:
		default:
			return PO_PROGRAM_IMAGE_MODULE_LOAD_FAILED;
	}
}

static PoProgramImageStatus po_reload_external_libraries(PocoVm* vm, const char* script_path,
														 Poco_lib** libraries)
{
	Poco_lib* names;
	Poco_lib* first = NULL;
	Poco_lib* tail = NULL;

	if (vm == NULL || libraries == NULL) {
		return PO_PROGRAM_IMAGE_INVALID_ARGUMENT;
	}
	names = *libraries;
	if (names == NULL) {
		return PO_PROGRAM_IMAGE_OK;
	}
	if (vm->poe_libraries_disabled) {
		poco_set_error(vm, "external libraries disabled for this VM");
		*libraries = NULL;
		return PO_PROGRAM_IMAGE_EXTERNAL_LIBRARIES_DISABLED;
	}
	for (; names != NULL; names = names->next) {
		Poco_lib* loaded = NULL;
		Errcode load_status =
			pj_load_pocorex(&loaded, script_path, names->name, NULL, vm->library_dirs,
							vm->verbose != 0, poco_vm_module_hooks(vm));

		if (load_status < Success) {
			PoProgramImageStatus image_status = po_external_library_status(load_status);
			poco_set_error(vm, "failed to reload external library '%s' (status %d)", names->name,
						   (int)load_status);
			pj_free_pocorexes(&first);
			*libraries = NULL;
			return image_status;
		}
		loaded->next = NULL;
		if (tail != NULL) {
			tail->next = loaded;
		} else {
			first = loaded;
		}
		tail = loaded;
	}
	*libraries = first;
	return PO_PROGRAM_IMAGE_OK;
}

static void po_decoded_structs_cleanup(PoDecodedStructs* structs)
{
	free(structs->items);
	free(structs->member_counts);
	memset(structs, 0, sizeof(*structs));
}

static PoProgramImageStatus po_decode_struct_headers(PoSectionView section, Poco_cb* owner,
													 PoDecodedStructs* out_structs)
{
	PoReader reader = {section.data, section.size, 0};
	uint32_t count;
	uint32_t index;

	memset(out_structs, 0, sizeof(*out_structs));
	if (!po_reader_u32(&reader, &count)) {
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	if (count != 0) {
		out_structs->items = calloc(count, sizeof(*out_structs->items));
		out_structs->member_counts = calloc(count, sizeof(*out_structs->member_counts));
		if (out_structs->items == NULL || out_structs->member_counts == NULL) {
			po_decoded_structs_cleanup(out_structs);
			return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		}
	}
	out_structs->count = count;
	for (index = 0; index < count; ++index) {
		uint32_t kind;
		uint32_t member_count;
		Struct_info* struct_info = po_memzalloc(owner, sizeof(*struct_info));

		if (struct_info == NULL) {
			po_decoded_structs_cleanup(out_structs);
			return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		}
		if (!po_reader_u32(&reader, &kind) || (kind != TYPE_STRUCT && kind != TYPE_UNION)) {
			po_decoded_structs_cleanup(out_structs);
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		struct_info->name = po_reader_arena_string(&reader, owner);
		if (struct_info->name == NULL || !po_reader_u32(&reader, &member_count) ||
			member_count > SHRT_MAX) {
			po_decoded_structs_cleanup(out_structs);
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		struct_info->type = (TypeComp)kind;
		struct_info->el_count = (SHORT)member_count;
		out_structs->items[index] = struct_info;
		out_structs->member_counts[index] = member_count;
		if (index != 0) {
			out_structs->items[index - 1]->next = struct_info;
		}
	}
	out_structs->members = reader;
	return PO_PROGRAM_IMAGE_OK;
}

static size_t po_decoded_struct_index(Struct_info** structs, size_t count,
									  const Struct_info* wanted)
{
	size_t index;
	for (index = 0; index < count; ++index) {
		if (structs[index] == wanted) {
			return index;
		}
	}
	return SIZE_MAX;
}

static PoProgramImageStatus po_layout_struct(PoDecodedStructs* structs, size_t struct_index,
											 uint8_t* states);

static PoProgramImageStatus po_layout_type(const Type_info* type, PoDecodedStructs* structs,
										   uint8_t* states, long* out_size)
{
	long size = 0;
	int known = 1;
	unsigned int index;

	if (type == NULL || type->comp_count == 0 || type->comp == NULL || type->sdims == NULL) {
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	for (index = 0; index < type->comp_count; ++index) {
		switch (type->comp[index]) {
			case TYPE_CHAR:
			case TYPE_UCHAR:
				size = sizeof(char);
				known = 1;
				break;
			case TYPE_SHORT:
			case TYPE_USHORT:
				size = sizeof(short);
				known = 1;
				break;
			case TYPE_INT:
			case TYPE_UINT:
				size = sizeof(int);
				known = 1;
				break;
			case TYPE_LONG:
			case TYPE_ULONG:
				size = sizeof(long);
				known = 1;
				break;
			case TYPE_FLOAT:
				size = sizeof(float);
				known = 1;
				break;
			case TYPE_DOUBLE:
				size = sizeof(double);
				known = 1;
				break;
			case TYPE_POINTER:
			case TYPE_SCREEN:
				size = sizeof(Popot);
				known = 1;
				break;
			case TYPE_CPT:
				size = sizeof(void*);
				known = 1;
				break;
			case TYPE_STRUCT: {
				size_t nested =
					po_decoded_struct_index(structs->items, structs->count, type->sdims[index].pt);
				PoProgramImageStatus status;
				if (nested == SIZE_MAX) {
					return PO_PROGRAM_IMAGE_MALFORMED;
				}
				if (states[nested] == 1) {
					known = 0;
					break;
				}
				status = po_layout_struct(structs, nested, states);
				if (status != PO_PROGRAM_IMAGE_OK) {
					return status;
				}
				size = structs->items[nested]->size;
				known = size != 0;
				break;
			}
			case TYPE_ARRAY: {
				long count = type->sdims[index].l;
				if (!known || count <= 0 || size > LONG_MAX / count) {
					return PO_PROGRAM_IMAGE_MALFORMED;
				}
				size *= count;
				break;
			}
			case TYPE_FUNCTION:
			case TYPE_VOID:
			case TYPE_ELLIPSIS:
			case TYPE_FILE:
				known = 0;
				size = 0;
				break;
			default:
				return PO_PROGRAM_IMAGE_MALFORMED;
		}
	}
	if (!known || size <= 0) {
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	*out_size = size;
	return PO_PROGRAM_IMAGE_OK;
}

static PoProgramImageStatus po_layout_struct(PoDecodedStructs* structs, size_t struct_index,
											 uint8_t* states)
{
	Struct_info* struct_info = structs->items[struct_index];
	Symbol* member;
	long size = 0;

	if (states[struct_index] == 2) {
		return PO_PROGRAM_IMAGE_OK;
	}
	if (states[struct_index] == 1) {
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	states[struct_index] = 1;
	for (member = struct_info->elements; member != NULL; member = member->next) {
		long member_size;
		PoProgramImageStatus status = po_layout_type(member->ti, structs, states, &member_size);
		if (status != PO_PROGRAM_IMAGE_OK) {
			states[struct_index] = 0;
			return status;
		}
		if (struct_info->type == TYPE_UNION) {
			member->symval.doff = 0;
			if (size < member_size) {
				size = member_size;
			}
		} else {
			member->symval.doff = size;
			if (size > LONG_MAX - member_size) {
				states[struct_index] = 0;
				return PO_PROGRAM_IMAGE_MALFORMED;
			}
			size += member_size;
		}
	}
	struct_info->size = size;
	states[struct_index] = 2;
	return PO_PROGRAM_IMAGE_OK;
}

static PoProgramImageStatus po_decode_struct_members(PoDecodedStructs* structs, Poco_cb* owner,
													 Func_frame** frames, size_t frame_count)
{
	PoReader reader = structs->members;
	uint8_t* states = NULL;
	size_t index;
	PoProgramImageStatus status = PO_PROGRAM_IMAGE_OK;

	for (index = 0; index < structs->count; ++index) {
		Struct_info* struct_info = structs->items[index];
		Symbol* first = NULL;
		Symbol* tail = NULL;
		uint32_t member_index;

		for (member_index = 0; member_index < structs->member_counts[index]; ++member_index) {
			Symbol* member = po_memzalloc(owner, sizeof(*member));
			if (member == NULL) {
				return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
			}
			member->name = po_reader_arena_string(&reader, owner);
			if (member->name == NULL) {
				return PO_PROGRAM_IMAGE_MALFORMED;
			}
			status = po_decode_type(&reader, owner, frames, frame_count, structs->items,
									structs->count, &member->ti);
			if (status != PO_PROGRAM_IMAGE_OK) {
				return status;
			}
			member->tok_type = PTOK_VAR;
			if (tail != NULL) {
				tail->next = member;
				tail->link = member;
			} else {
				first = member;
			}
			tail = member;
		}
		struct_info->elements = first;
	}
	if (reader.offset != reader.size) {
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	if (structs->count != 0) {
		states = calloc(structs->count, sizeof(*states));
		if (states == NULL) {
			return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		}
	}
	for (index = 0; index < structs->count; ++index) {
		status = po_layout_struct(structs, index, states);
		if (status != PO_PROGRAM_IMAGE_OK) {
			break;
		}
	}
	free(states);
	return status;
}

static int po_valid_reference(uint32_t reference, size_t frame_count)
{
	return reference == PO_IMAGE_NO_INDEX || reference < frame_count;
}

static PoProgramImageStatus po_decode_prototypes(PoSectionView section, Poco_cb* owner, PocoVm* vm,
												 const Poco_lib* loaded_libraries,
												 Struct_info** structs, size_t struct_count,
												 Func_frame*** out_frames, size_t* out_count)
{
	PoReader reader = {section.data, section.size, 0};
	uint32_t count;
	uint32_t index;
	Func_frame** frames;

	if (!po_reader_u32(&reader, &count) || count == 0) {
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	frames = calloc(count, sizeof(*frames));
	if (frames == NULL) {
		return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
	}
	for (index = 0; index < count; ++index) {
		frames[index] = po_memzalloc(owner, sizeof(*frames[index]));
		if (frames[index] == NULL) {
			free(frames);
			return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		}
	}
	for (index = 0; index < count; ++index) {
		Func_frame* frame = frames[index];
		uint32_t pcount;
		uint32_t type;
		uint32_t flags;
		uint64_t magic;
		uint32_t got_code;
		uint32_t next;
		uint32_t mlink;
		uint32_t compiled;
		uint32_t unit_index;
		uint32_t line_count;
		uint32_t line;
		PoProgramImageStatus status;

		frame->name = po_reader_arena_string(&reader, owner);
		if (frame->name == NULL || !po_reader_u32(&reader, &pcount) ||
			!po_reader_u32(&reader, &type) || !po_reader_u32(&reader, &flags) ||
			!po_reader_u64(&reader, &magic) || !po_reader_u32(&reader, &got_code) ||
			!po_reader_u32(&reader, &next) || !po_reader_u32(&reader, &mlink) ||
			!po_reader_u32(&reader, &compiled) || !po_reader_u32(&reader, &unit_index) ||
			type > CFF_C || !po_valid_reference(next, count) || !po_valid_reference(mlink, count) ||
			!po_valid_reference(compiled, count)) {
			free(frames);
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		frame->pcount = (short)(int32_t)pcount;
		if ((int32_t)frame->pcount != (int32_t)pcount || frame->pcount < 0) {
			free(frames);
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		frame->type = (short)type;
		frame->binding_flags = flags;
		frame->magic = (long)(int64_t)magic;
		if ((int64_t)frame->magic != (int64_t)magic) {
			free(frames);
			return PO_PROGRAM_IMAGE_UNSUPPORTED;
		}
		frame->got_code = got_code != 0;
		frame->next = next != PO_IMAGE_NO_INDEX ? frames[next] : NULL;
		frame->mlink = mlink != PO_IMAGE_NO_INDEX ? frames[mlink] : NULL;
		frame->compiled_frame = compiled != PO_IMAGE_NO_INDEX ? frames[compiled] : NULL;
		frame->unit_index = unit_index;
		status = po_decode_type(&reader, owner, frames, count, structs, struct_count,
								&frame->return_type);
		if (status != PO_PROGRAM_IMAGE_OK) {
			free(frames);
			return status;
		}
		if (!po_reader_u32(&reader, &line_count) || line_count > INT_MAX ||
			(line_count != 0 && SIZE_MAX / (size_t)line_count < 2 * sizeof(long)) ||
			line_count > (reader.size - reader.offset) / 20u) {
			free(frames);
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		if (line_count != 0) {
			size_t bytes = (size_t)line_count * 2 * sizeof(long);
			long* values;
			frame->ld = po_memzalloc(owner, sizeof(*frame->ld));
			values = po_memzalloc(owner, bytes);
			if (frame->ld == NULL || values == NULL) {
				free(frames);
				return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
			}
			frame->ld->count = frame->ld->alloc = (int)line_count;
			frame->ld->offsets = values;
			frame->ld->lines = values + line_count;
			/* offsets are read as instruction ordinals and stay ordinals until
			 * po_relocate_line_offsets rewrites them to native byte offsets,
			 * once po_decode_code has rebuilt this frame's native code. */
			for (line = 0; line < line_count; ++line) {
				uint64_t value;
				uint32_t file_index;
				if (!po_reader_u64(&reader, &value)) {
					free(frames);
					return PO_PROGRAM_IMAGE_MALFORMED;
				}
				frame->ld->offsets[line] = (long)(int64_t)value;
				if ((int64_t)frame->ld->offsets[line] != (int64_t)value) {
					free(frames);
					return PO_PROGRAM_IMAGE_UNSUPPORTED;
				}
				if (!po_reader_u32(&reader, &file_index) || file_index != unit_index) {
					free(frames);
					return PO_PROGRAM_IMAGE_MALFORMED;
				}
				if (!po_reader_u64(&reader, &value)) {
					free(frames);
					return PO_PROGRAM_IMAGE_MALFORMED;
				}
				frame->ld->lines[line] = (long)(int64_t)value;
				if ((int64_t)frame->ld->lines[line] != (int64_t)value) {
					free(frames);
					return PO_PROGRAM_IMAGE_UNSUPPORTED;
				}
			}
		}
		if (frame->type == CFF_C) {
			void* function;
			const PocoBindingContract* contract;
			uint32_t binding_flags;
			if (!po_vm_resolve_serialized_binding(vm, loaded_libraries, frame->name, &function,
												  &contract, &binding_flags)) {
				free(frames);
				return PO_PROGRAM_IMAGE_UNKNOWN_BINDING;
			}
			if (binding_flags != frame->binding_flags) {
				free(frames);
				return PO_PROGRAM_IMAGE_UNKNOWN_BINDING;
			}
			frame->code_pt = function;
			frame->binding_contract = contract;
		}
	}
	if (reader.offset != reader.size) {
		free(frames);
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	*out_frames = frames;
	*out_count = count;
	return PO_PROGRAM_IMAGE_OK;
}

static PoProgramImageStatus po_decode_symbols(PoSectionView section, Poco_cb* owner,
											  Func_frame** frames, size_t frame_count,
											  Struct_info** structs, size_t struct_count)
{
	PoReader reader = {section.data, section.size, 0};
	uint32_t count;
	uint32_t index;

	if (!po_reader_u32(&reader, &count) || count != frame_count) {
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	for (index = 0; index < count; ++index) {
		uint32_t symbol_count;
		uint32_t symbol_index;
		Symbol* first = NULL;
		Symbol* tail = NULL;

		if (!po_reader_u32(&reader, &symbol_count)) {
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		for (symbol_index = 0; symbol_index < symbol_count; ++symbol_index) {
			Symbol* symbol = po_memzalloc(owner, sizeof(*symbol));
			uint64_t value;
			uint32_t tok;
			uint32_t scope;
			uint32_t storage;
			uint32_t flags;
			PoProgramImageStatus status;

			if (symbol == NULL) {
				return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
			}
			symbol->name = po_reader_arena_string(&reader, owner);
			if (symbol->name == NULL || !po_reader_u64(&reader, &value) ||
				!po_reader_u32(&reader, &tok) || !po_reader_u32(&reader, &scope) ||
				!po_reader_u32(&reader, &storage) || !po_reader_u32(&reader, &flags) ||
				storage > SCOPE_LOCAL) {
				return PO_PROGRAM_IMAGE_MALFORMED;
			}
			symbol->symval.l = (long)(int64_t)value;
			symbol->tok_type = (PToken_t)tok;
			symbol->scope = (SHORT)(int32_t)scope;
			symbol->storage_scope = (StorageScope)storage;
			symbol->flags = (SymbolFlags)flags;
			status = po_decode_type(&reader, owner, frames, frame_count, structs, struct_count,
									&symbol->ti);
			if (status != PO_PROGRAM_IMAGE_OK) {
				return status;
			}
			if (tail != NULL) {
				tail->link = symbol;
			} else {
				first = symbol;
			}
			tail = symbol;
		}
		frames[index]->parameters = first;
		if (symbol_count != (uint32_t)frames[index]->pcount) {
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
	}
	return reader.offset == reader.size ? PO_PROGRAM_IMAGE_OK : PO_PROGRAM_IMAGE_MALFORMED;
}

static PoProgramImageStatus po_decode_debug_locals(PoSectionView section, Poco_cb* owner,
												   Func_frame** frames, size_t frame_count,
												   Struct_info** structs, size_t struct_count,
												   size_t* out_minimal_struct_count)
{
	PoReader reader = {section.data, section.size, 0};
	uint32_t minimal_struct_count;
	uint32_t count;
	uint32_t frame_index;

	if (!po_reader_u32(&reader, &minimal_struct_count) || minimal_struct_count > struct_count ||
		!po_reader_u32(&reader, &count) || count != frame_count) {
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	*out_minimal_struct_count = minimal_struct_count;
	for (frame_index = 0; frame_index < count; ++frame_index) {
		PocoDebugLocal** tail = &frames[frame_index]->debug_locals;
		uint32_t local_count;
		uint32_t local_index;

		if (!po_reader_u32(&reader, &local_count)) {
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		for (local_index = 0; local_index < local_count; ++local_index) {
			PocoDebugLocal* local = po_memzalloc(owner, sizeof(*local));
			uint64_t frame_offset;
			uint32_t scope;
			uint32_t storage_scope;
			uint64_t live_start;
			uint64_t live_end;
			uint32_t approximate;
			PoProgramImageStatus status;

			if (local == NULL) {
				return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
			}
			local->name = po_reader_arena_string(&reader, owner);
			if (local->name == NULL || local->name[0] == '\0' ||
				!po_reader_u64(&reader, &frame_offset) || !po_reader_u32(&reader, &scope) ||
				!po_reader_u32(&reader, &storage_scope) || !po_reader_u64(&reader, &live_start) ||
				!po_reader_u64(&reader, &live_end) || !po_reader_u32(&reader, &approximate) ||
				storage_scope > SCOPE_LOCAL || approximate > 1) {
				return PO_PROGRAM_IMAGE_MALFORMED;
			}
			local->frame_offset = (long)(int64_t)frame_offset;
			local->scope = (SHORT)(int32_t)scope;
			local->storage_scope = (StorageScope)storage_scope;
			local->live_range_approximate = approximate != 0;
			if ((int64_t)local->frame_offset != (int64_t)frame_offset ||
				(int32_t)local->scope != (int32_t)scope || live_start > live_end) {
				return PO_PROGRAM_IMAGE_UNSUPPORTED;
			}
			status = po_instruction_to_native_offset(frames[frame_index], live_start,
													 &local->live_start);
			if (status == PO_PROGRAM_IMAGE_OK) {
				status = po_instruction_to_native_offset(frames[frame_index], live_end,
														 &local->live_end);
			}
			if (status == PO_PROGRAM_IMAGE_OK) {
				status = po_decode_type(&reader, owner, frames, frame_count, structs, struct_count,
										&local->type);
			}
			if (status != PO_PROGRAM_IMAGE_OK) {
				return status;
			}
			if (local->type == NULL) {
				return PO_PROGRAM_IMAGE_MALFORMED;
			}
			*tail = local;
			tail = &local->next;
		}
	}
	return reader.offset == reader.size ? PO_PROGRAM_IMAGE_OK : PO_PROGRAM_IMAGE_MALFORMED;
}

const PocoDebugLocal* po_program_debug_local_lookup(const Func_frame* frame, const char* name,
													long bytecode_offset, short scope)
{
	const PocoDebugLocal* local;
	const PocoDebugLocal* best = NULL;

	if (frame == NULL || name == NULL || bytecode_offset < 0) {
		return NULL;
	}
	for (local = frame->debug_locals; local != NULL; local = local->next) {
		if (local->scope <= scope && local->live_start <= bytecode_offset &&
			bytecode_offset < local->live_end && strcmp(local->name, name) == 0 &&
			(best == NULL || local->scope > best->scope)) {
			best = local;
		}
	}
	return best;
}

static PoProgramImageStatus po_decode_code_frame(PoReader* encoded, Poco_cb* owner,
												 Func_frame* frame, Func_frame** frames,
												 size_t frame_count, const Names* literals)
{
	PoWriter native = {0};
	PoProgramImageStatus status = PO_PROGRAM_IMAGE_OK;

	while (encoded->offset < encoded->size) {
		uint32_t op_value;
		int op;
		Poco_op_table* entry;
		if (!po_reader_u32(encoded, &op_value) || op_value >= (uint32_t)po_ins_table_els) {
			status = PO_PROGRAM_IMAGE_MALFORMED;
			goto CLEANUP;
		}
		op = (int)op_value;
		entry = &po_ins_table[op];
		if (!po_writer_bytes(&native, &op, sizeof(op))) {
			status = PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
			goto CLEANUP;
		}
		switch (entry->op_ext) {
			case OEX_NONE:
				break;
			case OEX_VAR:
			case OEX_INT:
			case OEX_LABEL: {
				uint32_t value;
				int native_value;
				if (!po_reader_u32(encoded, &value)) {
					status = PO_PROGRAM_IMAGE_MALFORMED;
					goto CLEANUP;
				}
				native_value = (int)(int32_t)value;
				if (!po_writer_bytes(&native, &native_value, sizeof(native_value))) {
					status = PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
				}
				break;
			}
			case OEX_LONG: {
				uint64_t value;
				long native_value;
				if (!po_reader_u64(encoded, &value)) {
					status = PO_PROGRAM_IMAGE_MALFORMED;
					goto CLEANUP;
				}
				native_value = (long)(int64_t)value;
				if ((int64_t)native_value != (int64_t)value) {
					status = PO_PROGRAM_IMAGE_UNSUPPORTED;
				} else if (!po_writer_bytes(&native, &native_value, sizeof(native_value))) {
					status = PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
				}
				break;
			}
			case OEX_DOUBLE: {
				uint64_t bits;
				double value;
				if (!po_reader_u64(encoded, &bits)) {
					status = PO_PROGRAM_IMAGE_MALFORMED;
					goto CLEANUP;
				}
				memcpy(&value, &bits, sizeof(value));
				if (!po_writer_bytes(&native, &value, sizeof(value))) {
					status = PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
				}
				break;
			}
			case OEX_ADDRESS: {
				uint32_t offset;
				uint64_t span;
				int native_offset;
				long native_span;
				if (!po_reader_u32(encoded, &offset) || !po_reader_u64(encoded, &span)) {
					status = PO_PROGRAM_IMAGE_MALFORMED;
					goto CLEANUP;
				}
				native_offset = (int)(int32_t)offset;
				native_span = (long)(int64_t)span;
				if ((int64_t)native_span != (int64_t)span) {
					status = PO_PROGRAM_IMAGE_UNSUPPORTED;
				} else if (!po_writer_bytes(&native, &native_offset, sizeof(native_offset)) ||
						   !po_writer_bytes(&native, &native_span, sizeof(native_span))) {
					status = PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
				}
				break;
			}
			case OEX_FUNCTION:
			case OEX_CFUNCTION: {
				uint32_t reference;
				void* pointer;
				if (!po_reader_u32(encoded, &reference) || reference >= frame_count) {
					status = PO_PROGRAM_IMAGE_MALFORMED;
					goto CLEANUP;
				}
				pointer = entry->op_ext == OEX_CFUNCTION ? (void*)frames[reference]->code_pt
														 : frames[reference];
				if ((entry->op_ext == OEX_CFUNCTION && frames[reference]->type != CFF_C) ||
					!po_writer_bytes(&native, &pointer, sizeof(pointer))) {
					status = entry->op_ext == OEX_CFUNCTION ? PO_PROGRAM_IMAGE_MALFORMED
															: PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
				}
				break;
			}
			case OEX_POINTER: {
				uint32_t kind;
				Popot pointer;
				memset(&pointer, 0, sizeof(pointer));
				if (!po_reader_u32(encoded, &kind)) {
					status = PO_PROGRAM_IMAGE_MALFORMED;
					goto CLEANUP;
				}
				if (kind == 0) {
					pointer.min = (void*)1;
				} else if (kind == 1) {
					uint32_t literal_index;
					uint64_t minimum;
					uint64_t maximum;
					uint64_t current;
					const Names* literal;
					size_t length;
					if (!po_reader_u32(encoded, &literal_index) ||
						!po_reader_u64(encoded, &minimum) || !po_reader_u64(encoded, &maximum) ||
						!po_reader_u64(encoded, &current) ||
						(literal = po_literal_at(literals, literal_index)) == NULL) {
						status = PO_PROGRAM_IMAGE_MALFORMED;
						goto CLEANUP;
					}
					length = strlen(literal->name);
					if (minimum > current || current > maximum || maximum > length) {
						status = PO_PROGRAM_IMAGE_MALFORMED;
						goto CLEANUP;
					}
					pointer.min = literal->name + minimum;
					pointer.max = literal->name + maximum;
					pointer.pt = literal->name + current;
				} else {
					status = PO_PROGRAM_IMAGE_MALFORMED;
					goto CLEANUP;
				}
				if (!po_writer_bytes(&native, &pointer, sizeof(pointer))) {
					status = PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
				}
				break;
			}
			default:
				status = PO_PROGRAM_IMAGE_UNSUPPORTED;
				break;
		}
		if (status != PO_PROGRAM_IMAGE_OK) {
			goto CLEANUP;
		}
	}
	if (native.size > LONG_MAX) {
		status = PO_PROGRAM_IMAGE_UNSUPPORTED;
		goto CLEANUP;
	}
	if (frame->type == CFF_C && native.size != 0) {
		status = PO_PROGRAM_IMAGE_MALFORMED;
		goto CLEANUP;
	}
	if (frame->type == CFF_POCO && native.size != 0) {
		frame->code_pt = po_memzalloc(owner, native.size);
		if (frame->code_pt == NULL) {
			status = PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
			goto CLEANUP;
		}
		memcpy(frame->code_pt, native.data, native.size);
	}
	frame->code_size = (long)native.size;

CLEANUP:
	free(native.data);
	return status;
}

static PoProgramImageStatus po_decode_code(PoSectionView section, Poco_cb* owner,
										   Func_frame** frames, size_t frame_count,
										   const Names* literals)
{
	PoReader reader = {section.data, section.size, 0};
	uint32_t count;
	uint32_t index;
	if (!po_reader_u32(&reader, &count) || count != frame_count) {
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	for (index = 0; index < count; ++index) {
		uint64_t encoded_size;
		PoReader encoded;
		PoProgramImageStatus status;
		if (!po_reader_u64(&reader, &encoded_size) || encoded_size > SIZE_MAX ||
			reader.offset > reader.size || (size_t)encoded_size > reader.size - reader.offset) {
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		encoded.data = reader.data + reader.offset;
		encoded.size = (size_t)encoded_size;
		encoded.offset = 0;
		status =
			po_decode_code_frame(&encoded, owner, frames[index], frames, frame_count, literals);
		if (status != PO_PROGRAM_IMAGE_OK || encoded.offset != encoded.size) {
			return status != PO_PROGRAM_IMAGE_OK ? status : PO_PROGRAM_IMAGE_MALFORMED;
		}
		reader.offset += (size_t)encoded_size;
	}
	return reader.offset == reader.size ? PO_PROGRAM_IMAGE_OK : PO_PROGRAM_IMAGE_MALFORMED;
}

static PoProgramImageStatus po_parse_header(const void* image, size_t image_size,
											PoSectionView sections[PO_IMAGE_MAX_SECTION_COUNT],
											PocoDebugLevel debug_level, long* out_stack_size,
											long* out_data_size, uint32_t* out_functions,
											uint32_t* out_prototypes, uint32_t* out_version)
{
	PoReader reader;
	uint8_t magic[8];
	uint32_t version;
	uint32_t section_count;
	uint64_t stack_size;
	uint64_t data_size;
	uint32_t index;
	uint32_t base_section_count = debug_level == POCO_DEBUG_LEVEL_EXTENDED
									  ? PO_IMAGE_EXTENDED_SECTION_COUNT
									  : PO_IMAGE_MINIMAL_SECTION_COUNT;
	uint32_t expected_section_count;
	size_t expected_offset;
	static const uint32_t expected[PO_IMAGE_MAX_SECTION_COUNT] = {
		PO_PROGRAM_IMAGE_SECTION_CODE,
		PO_PROGRAM_IMAGE_SECTION_CONSTANTS,
		PO_PROGRAM_IMAGE_SECTION_PROTOTYPES,
		PO_PROGRAM_IMAGE_SECTION_SYMBOLS,
		PO_PROGRAM_IMAGE_SECTION_STRUCT_DEFINITIONS,
		PO_PROGRAM_IMAGE_SECTION_DEBUG_LOCALS,
		PO_PROGRAM_IMAGE_SECTION_EXTERNAL_LIBRARIES};

	if (image == NULL || image_size < 12u) {
		return PO_PROGRAM_IMAGE_TRUNCATED;
	}
	reader.data = image;
	reader.size = image_size;
	reader.offset = 0;
	if (!po_reader_bytes(&reader, magic, sizeof(magic)) ||
		memcmp(magic, po_image_magic, sizeof(magic)) != 0 || !po_reader_u32(&reader, &version)) {
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	if (version != PO_IMAGE_VERSION_MULTI_SOURCE) {
		return PO_PROGRAM_IMAGE_VERSION_MISMATCH;
	}
	expected_section_count = base_section_count + 1u;
	expected_offset = PO_IMAGE_HEADER_SIZE(expected_section_count);
	if (image_size < expected_offset) {
		return PO_PROGRAM_IMAGE_TRUNCATED;
	}
	if (!po_reader_u32(&reader, &section_count) || section_count != expected_section_count ||
		!po_reader_u64(&reader, &stack_size) || !po_reader_u64(&reader, &data_size) ||
		!po_reader_u32(&reader, out_functions) || !po_reader_u32(&reader, out_prototypes)) {
		return PO_PROGRAM_IMAGE_MALFORMED;
	}
	*out_stack_size = (long)(int64_t)stack_size;
	*out_data_size = (long)(int64_t)data_size;
	if ((int64_t)*out_stack_size != (int64_t)stack_size ||
		(int64_t)*out_data_size != (int64_t)data_size || *out_stack_size < 0 ||
		*out_data_size < 0) {
		return PO_PROGRAM_IMAGE_UNSUPPORTED;
	}
	for (index = 0; index < expected_section_count; ++index) {
		uint32_t expected_kind = index < base_section_count
									 ? expected[index]
									 : PO_PROGRAM_IMAGE_SECTION_EXTERNAL_LIBRARIES;
		uint32_t kind;
		uint32_t reserved;
		uint64_t offset;
		uint64_t size;
		if (!po_reader_u32(&reader, &kind) || !po_reader_u32(&reader, &reserved) ||
			!po_reader_u64(&reader, &offset) || !po_reader_u64(&reader, &size) ||
			kind != expected_kind || reserved != 0 || offset > SIZE_MAX || size > SIZE_MAX ||
			(size_t)offset != expected_offset || (size_t)offset > image_size) {
			return PO_PROGRAM_IMAGE_MALFORMED;
		}
		if ((size_t)size > image_size - (size_t)offset) {
			return PO_PROGRAM_IMAGE_TRUNCATED;
		}
		sections[index].data = (const uint8_t*)image + (size_t)offset;
		sections[index].size = (size_t)size;
		expected_offset += (size_t)size;
	}
	*out_version = version;
	return expected_offset == image_size ? PO_PROGRAM_IMAGE_OK : PO_PROGRAM_IMAGE_MALFORMED;
}

/*
 * Rewrite each frame's opcode<->line map from the portable instruction ordinals
 * stored in the image back to native byte offsets. Must run after po_decode_code
 * has rebuilt native code, since the ordinal->offset mapping walks it. Only
 * compiled frames carry a line map; C-binding frames have none.
 */
static PoProgramImageStatus po_relocate_line_offsets(Func_frame** frames, size_t frame_count)
{
	size_t index;

	for (index = 0; index < frame_count; ++index) {
		Func_frame* frame = frames[index];
		int line;

		if (frame->ld == NULL || frame->code_pt == NULL) {
			continue;
		}
		for (line = 0; line < frame->ld->count; ++line) {
			long native;
			PoProgramImageStatus status =
				po_instruction_to_native_offset(frame, (uint64_t)frame->ld->offsets[line], &native);
			if (status != PO_PROGRAM_IMAGE_OK) {
				return status;
			}
			frame->ld->offsets[line] = native;
		}
	}
	return PO_PROGRAM_IMAGE_OK;
}

PoProgramImageStatus po_program_image_decode(PocoVm* vm, const void* image, size_t image_size,
											 PocoDebugLevel debug_level, const char* script_path,
											 PocoProgram** out_program)
{
	PoSectionView sections[PO_IMAGE_MAX_SECTION_COUNT] = {{0}};
	PoProgramImageStatus status;
	Poco_cb* owner = NULL;
	Poco_run_env* executable = NULL;
	Func_frame** frames = NULL;
	size_t frame_count = 0;
	long stack_size;
	long data_size;
	uint32_t functions;
	uint32_t prototypes;
	uint32_t image_version;
	PocoStatus adopt_status;
	PoDecodedStructs structs = {0};
	size_t minimal_struct_count = 0;

	if (vm == NULL || out_program == NULL ||
		(debug_level != POCO_DEBUG_LEVEL_MINIMAL && debug_level != POCO_DEBUG_LEVEL_EXTENDED)) {
		return PO_PROGRAM_IMAGE_INVALID_ARGUMENT;
	}
	*out_program = NULL;
	status = po_parse_header(image, image_size, sections, debug_level, &stack_size, &data_size,
							 &functions, &prototypes, &image_version);
	if (status != PO_PROGRAM_IMAGE_OK) {
		return status;
	}
	if (po_init_memory_management(&owner) != Success) {
		return PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
	}
	executable = pj_zalloc(sizeof(*executable));
	if (executable == NULL) {
		status = PO_PROGRAM_IMAGE_OUT_OF_MEMORY;
		goto CLEANUP;
	}
	executable->compile_pcb = owner;
	executable->stack_size = stack_size;
	executable->data_size = data_size;
	{
		uint32_t library_section = debug_level == POCO_DEBUG_LEVEL_EXTENDED
									   ? PO_IMAGE_EXTENDED_SECTION_COUNT
									   : PO_IMAGE_MINIMAL_SECTION_COUNT;
		status = po_decode_external_libraries(sections[library_section], owner,
											  &executable->loaded_libs);
		if (status != PO_PROGRAM_IMAGE_OK) {
			goto CLEANUP;
		}
		if (executable->loaded_libs != NULL) {
			status = po_reload_external_libraries(vm, script_path, &executable->loaded_libs);
			if (status != PO_PROGRAM_IMAGE_OK) {
				goto CLEANUP;
			}
		}
	}
	status = po_decode_constants(sections[1], owner, &executable->literals);
	if (status != PO_PROGRAM_IMAGE_OK) {
		goto CLEANUP;
	}
	status = po_decode_struct_headers(sections[4], owner, &structs);
	if (status != PO_PROGRAM_IMAGE_OK) {
		goto CLEANUP;
	}
	executable->struct_infos = structs.count != 0 ? structs.items[0] : NULL;
	status = po_decode_prototypes(sections[2], owner, vm, executable->loaded_libs, structs.items,
								  structs.count, &frames, &frame_count);
	if (status != PO_PROGRAM_IMAGE_OK) {
		goto CLEANUP;
	}
	if (functions >= frame_count || prototypes >= frame_count) {
		status = PO_PROGRAM_IMAGE_MALFORMED;
		goto CLEANUP;
	}
	executable->fff = frames[functions];
	executable->protos = frames[prototypes];
	status =
		po_decode_symbols(sections[3], owner, frames, frame_count, structs.items, structs.count);
	if (status != PO_PROGRAM_IMAGE_OK) {
		goto CLEANUP;
	}
	status = po_decode_struct_members(&structs, owner, frames, frame_count);
	if (status != PO_PROGRAM_IMAGE_OK) {
		goto CLEANUP;
	}
	status = po_decode_code(sections[0], owner, frames, frame_count, executable->literals);
	if (status != PO_PROGRAM_IMAGE_OK) {
		goto CLEANUP;
	}
	status = po_relocate_line_offsets(frames, frame_count);
	if (status != PO_PROGRAM_IMAGE_OK) {
		goto CLEANUP;
	}
	if (debug_level == POCO_DEBUG_LEVEL_EXTENDED) {
		status = po_decode_debug_locals(sections[5], owner, frames, frame_count, structs.items,
										structs.count, &minimal_struct_count);
		if (status != PO_PROGRAM_IMAGE_OK) {
			goto CLEANUP;
		}
	} else {
		minimal_struct_count = structs.count;
	}
	adopt_status = po_program_adopt_decoded(vm, executable, out_program);
	if (adopt_status != POCO_STATUS_OK) {
		status = adopt_status == POCO_STATUS_OUT_OF_MEMORY ? PO_PROGRAM_IMAGE_OUT_OF_MEMORY
														   : PO_PROGRAM_IMAGE_UNKNOWN_BINDING;
		goto CLEANUP;
	}
	executable = NULL;
	owner = NULL;
	(*out_program)->minimal_struct_count = minimal_struct_count;
	(*out_program)->minimal_struct_count_known = 1;
	status = PO_PROGRAM_IMAGE_OK;

CLEANUP:
	free(frames);
	po_decoded_structs_cleanup(&structs);
	if (executable != NULL) {
		po_ffi_free_structures(executable);
		pj_free_pocorexes(&executable->loaded_libs);
		pj_free(executable);
	}
	if (owner != NULL) {
		po_free_all_memory(owner);
	}
	return status;
}
