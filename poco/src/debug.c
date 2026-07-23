/*******************************************************************************
 * debug.c - Activation-local source locations and line breakpoints.
 ******************************************************************************/

#include "debug_internal.h"
#include "program_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct PocoLineBreakpoint {
	const Func_frame* frame;
	long offset;
	long line;
} PocoLineBreakpoint;

typedef enum PocoDebugRunMode {
	POCO_DEBUG_RUN_CONTINUE,
	POCO_DEBUG_RUN_STEP_INTO,
	POCO_DEBUG_RUN_NEXT_OVER
} PocoDebugRunMode;

struct PocoDebugSession {
	PocoActivation* activation;
	PocoDebugPauseCallback pause_callback;
	void* user_data;
	PocoLineBreakpoint* line_targets;
	size_t line_target_count;
	PocoLineBreakpoint* breakpoints;
	size_t breakpoint_count;
	size_t breakpoint_capacity;
	const Func_frame* current_frame;
	long current_offset;
	long current_line;
	void* current_base;
	void* current_stack_start;
	void* current_stack_end;
	PocoDebugRunMode run_mode;
	const Func_frame* step_frame;
	long step_line;
	size_t current_depth;
	size_t step_depth;
	unsigned int pause_depth;
	int detach_requested;
};

static const Func_frame* po_debug_find_frame(const PocoActivation* activation,
											 const Code* instruction)
{
	const Func_frame* frame;
	uintptr_t address = (uintptr_t)instruction;

	for (frame = activation->code->functions; frame != NULL; frame = frame->next) {
		uintptr_t start = (uintptr_t)frame->code_pt;

		if (address >= start && address - start < (uintptr_t)frame->code_size) {
			return frame;
		}
	}
	return NULL;
}

static int po_debug_line_for_offset(const Line_data* data, long offset, long* out_line)
{
	int low;
	int high;

	if (data == NULL || data->count <= 0 || data->offsets == NULL || data->lines == NULL ||
		offset < data->offsets[0]) {
		return 0;
	}
	low = 0;
	high = data->count;
	while (low < high) {
		int middle = low + (high - low) / 2;

		if (data->offsets[middle] <= offset) {
			low = middle + 1;
		} else {
			high = middle;
		}
	}
	*out_line = data->lines[low - 1];
	return 1;
}

static int po_debug_line_target_compare(const void* left_pointer, const void* right_pointer)
{
	const PocoLineBreakpoint* left = left_pointer;
	const PocoLineBreakpoint* right = right_pointer;
	uintptr_t left_frame;
	uintptr_t right_frame;

	if (left->line < right->line) {
		return -1;
	}
	if (left->line > right->line) {
		return 1;
	}
	left_frame = (uintptr_t)left->frame;
	right_frame = (uintptr_t)right->frame;
	if (left_frame < right_frame) {
		return -1;
	}
	if (left_frame > right_frame) {
		return 1;
	}
	return left->offset < right->offset ? -1 : left->offset > right->offset;
}

static PocoStatus po_debug_build_line_targets(PocoDebugSession* session)
{
	const Func_frame* frame;
	size_t target_count = 0;
	size_t write_index;

	for (frame = session->activation->code->functions; frame != NULL; frame = frame->next) {
		if (frame->ld != NULL && frame->ld->count > 0) {
			if ((size_t)frame->ld->count > SIZE_MAX - target_count) {
				return POCO_STATUS_OVERFLOW;
			}
			target_count += (size_t)frame->ld->count;
		}
	}
	if (target_count == 0) {
		return POCO_STATUS_OK;
	}
	if (target_count > SIZE_MAX / sizeof(*session->line_targets)) {
		return POCO_STATUS_OVERFLOW;
	}
	session->line_targets = malloc(target_count * sizeof(*session->line_targets));
	if (session->line_targets == NULL) {
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	for (frame = session->activation->code->functions; frame != NULL; frame = frame->next) {
		int index;

		if (frame->ld == NULL || frame->ld->offsets == NULL || frame->ld->lines == NULL) {
			continue;
		}
		for (index = 0; index < frame->ld->count; ++index) {
			PocoLineBreakpoint* target;
			long mapped_line;

			if (!po_debug_line_for_offset(frame->ld, frame->ld->offsets[index], &mapped_line) ||
				mapped_line != frame->ld->lines[index]) {
				continue;
			}
			target = &session->line_targets[session->line_target_count++];

			target->frame = frame;
			target->offset = frame->ld->offsets[index];
			target->line = frame->ld->lines[index];
		}
	}
	qsort(session->line_targets, session->line_target_count, sizeof(*session->line_targets),
		  po_debug_line_target_compare);
	write_index = 0;
	for (target_count = 0; target_count < session->line_target_count; ++target_count) {
		PocoLineBreakpoint* target = &session->line_targets[target_count];

		if (target->line <= 0 ||
			(write_index != 0 && session->line_targets[write_index - 1].line == target->line &&
			 session->line_targets[write_index - 1].frame == target->frame)) {
			continue;
		}
		session->line_targets[write_index++] = *target;
	}
	session->line_target_count = write_index;
	return POCO_STATUS_OK;
}

static size_t po_debug_line_target_lower_bound(const PocoDebugSession* session, long line)
{
	size_t low = 0;
	size_t high = session->line_target_count;

	while (low < high) {
		size_t middle = low + (high - low) / 2;

		if (session->line_targets[middle].line < line) {
			low = middle + 1;
		} else {
			high = middle;
		}
	}
	return low;
}

static int po_debug_has_breakpoint(const PocoDebugSession* session, const Func_frame* frame,
								   long offset)
{
	size_t index;

	for (index = 0; index < session->breakpoint_count; ++index) {
		if (session->breakpoints[index].frame == frame &&
			session->breakpoints[index].offset == offset) {
			return 1;
		}
	}
	return 0;
}

static void po_debug_session_free(PocoDebugSession* session)
{
	if (session == NULL) {
		return;
	}
	free(session->line_targets);
	free(session->breakpoints);
	free(session);
}

PocoStatus poco_debug_attach(PocoActivation* activation, PocoDebugPauseCallback pause_callback,
							 void* user_data, PocoDebugSession** out_session)
{
	PocoDebugSession* session;

	if (activation == NULL || pause_callback == NULL || out_session == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	*out_session = NULL;
	if (activation->program == NULL || activation->debug_session != NULL ||
		activation->run_depth != 0) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	session = calloc(1, sizeof(*session));
	if (session == NULL) {
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	session->activation = activation;
	session->pause_callback = pause_callback;
	session->user_data = user_data;
	{
		PocoStatus status = po_debug_build_line_targets(session);

		if (status != POCO_STATUS_OK) {
			po_debug_session_free(session);
			return status;
		}
	}
	activation->debug_session = session;
	activation->debug_hook = po_debug_instruction_hook;
	*out_session = session;
	return POCO_STATUS_OK;
}

void poco_debug_detach(PocoDebugSession* session)
{
	PocoActivation* activation;

	if (session == NULL) {
		return;
	}
	activation = session->activation;
	if (activation != NULL && activation->debug_session == session) {
		activation->debug_hook = NULL;
		activation->debug_session = NULL;
	}
	session->activation = NULL;
	if (session->pause_depth != 0) {
		session->detach_requested = 1;
		return;
	}
	po_debug_session_free(session);
}

PocoStatus poco_debug_current_location(const PocoDebugSession* session,
									   PocoDebugLocation* out_location)
{
	const PocoProgram* program;

	if (session == NULL || out_location == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	out_location->file = NULL;
	out_location->line = 0;
	if (session->activation == NULL || session->current_frame == NULL ||
		session->current_line <= 0) {
		return POCO_STATUS_NOT_FOUND;
	}
	program = session->activation->program;
	if (program->sources == NULL || session->current_frame->unit_index >= program->source_count) {
		return POCO_STATUS_NOT_FOUND;
	}
	{
		const PocoProgramSource* source = &program->sources[session->current_frame->unit_index];
		out_location->file = source->path != NULL ? source->path : source->name;
	}
	out_location->line = session->current_line;
	return POCO_STATUS_OK;
}

PocoStatus poco_debug_add_line_breakpoint(PocoDebugSession* session, long line)
{
	size_t first_target;
	size_t required_count;
	size_t target_index;

	if (session == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (session->activation == NULL || line <= 0) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	for (required_count = 0; required_count < session->breakpoint_count; ++required_count) {
		if (session->breakpoints[required_count].line == line) {
			return POCO_STATUS_OK;
		}
	}
	first_target = po_debug_line_target_lower_bound(session, line);
	if (first_target == session->line_target_count ||
		session->line_targets[first_target].line != line) {
		return POCO_STATUS_NOT_FOUND;
	}
	for (target_index = first_target; target_index < session->line_target_count &&
									  session->line_targets[target_index].line == line;
		 ++target_index) {
	}
	if (target_index - first_target > SIZE_MAX - session->breakpoint_count) {
		return POCO_STATUS_OVERFLOW;
	}
	required_count = session->breakpoint_count + target_index - first_target;
	if (required_count > SIZE_MAX / sizeof(*session->breakpoints)) {
		return POCO_STATUS_OVERFLOW;
	}
	if (required_count > session->breakpoint_capacity) {
		size_t capacity = session->breakpoint_capacity != 0 ? session->breakpoint_capacity : 4;
		PocoLineBreakpoint* resized;

		while (capacity < required_count) {
			if (capacity > SIZE_MAX / 2 || capacity > SIZE_MAX / sizeof(*resized)) {
				return POCO_STATUS_OVERFLOW;
			}
			capacity *= 2;
		}
		if (capacity > SIZE_MAX / sizeof(*resized)) {
			return POCO_STATUS_OVERFLOW;
		}
		resized = realloc(session->breakpoints, capacity * sizeof(*resized));
		if (resized == NULL) {
			return POCO_STATUS_OUT_OF_MEMORY;
		}
		session->breakpoints = resized;
		session->breakpoint_capacity = capacity;
	}
	for (target_index = first_target; target_index < session->line_target_count &&
									  session->line_targets[target_index].line == line;
		 ++target_index) {
		session->breakpoints[session->breakpoint_count++] = session->line_targets[target_index];
	}
	return POCO_STATUS_OK;
}

PocoStatus poco_debug_clear_line_breakpoint(PocoDebugSession* session, long line)
{
	size_t read_index;
	size_t write_index = 0;

	if (session == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (session->activation == NULL || line <= 0) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	for (read_index = 0; read_index < session->breakpoint_count; ++read_index) {
		if (session->breakpoints[read_index].line != line) {
			session->breakpoints[write_index++] = session->breakpoints[read_index];
		}
	}
	if (write_index == session->breakpoint_count) {
		return POCO_STATUS_NOT_FOUND;
	}
	session->breakpoint_count = write_index;
	return POCO_STATUS_OK;
}

static PocoStatus po_debug_set_run_mode(PocoDebugSession* session, PocoDebugRunMode mode)
{
	if (session == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (session->activation == NULL) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	/* A fresh or reset activation has no current frame yet.  Let step/next arm
	 * the session before the first run so the first mapped instruction becomes
	 * the initial synchronous pause.  Continue remains a pause-only API, and a
	 * non-paused activation that has begun running cannot be retargeted here. */
	if (session->pause_depth == 0) {
		if (mode == POCO_DEBUG_RUN_CONTINUE || session->activation->run_depth != 0 ||
			session->current_frame != NULL) {
			return POCO_STATUS_PARAMETER_RANGE;
		}
		session->run_mode = mode;
		session->step_frame = NULL;
		session->step_line = 0;
		session->step_depth = 0;
		return POCO_STATUS_OK;
	}
	if (session->current_frame == NULL) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	session->run_mode = mode;
	session->step_frame = session->current_frame;
	session->step_line = session->current_line;
	session->step_depth = session->current_depth;
	return POCO_STATUS_OK;
}

PocoStatus poco_debug_step_into(PocoDebugSession* session)
{
	return po_debug_set_run_mode(session, POCO_DEBUG_RUN_STEP_INTO);
}

PocoStatus poco_debug_next_over(PocoDebugSession* session)
{
	return po_debug_set_run_mode(session, POCO_DEBUG_RUN_NEXT_OVER);
}

PocoStatus poco_debug_continue(PocoDebugSession* session)
{
	return po_debug_set_run_mode(session, POCO_DEBUG_RUN_CONTINUE);
}

static PocoCallbackValue po_debug_invalid_value(void)
{
	PocoCallbackValue value = {POCO_CALLBACK_VALUE_INVALID, {0}};

	return value;
}

static int po_debug_popot_is_bounded(Popot value)
{
	uintptr_t current;
	uintptr_t minimum;
	uintptr_t maximum;

	if (value.pt == NULL) {
		return 1;
	}
	if (value.min == NULL || value.max == NULL) {
		return 0;
	}
	current = (uintptr_t)value.pt;
	minimum = (uintptr_t)value.min;
	maximum = (uintptr_t)value.max;
	return minimum <= current && current <= maximum;
}

static const void* po_debug_offset_storage(const void* origin, long offset, const void* start,
										   const void* end, size_t value_size)
{
	uintptr_t address = (uintptr_t)origin;
	uintptr_t first = (uintptr_t)start;
	uintptr_t limit = (uintptr_t)end;
	uintptr_t delta;

	if (first > limit || address < first || address > limit) {
		return NULL;
	}
	if (offset < 0) {
		delta = (uintptr_t)(-(offset + 1)) + 1;
		if (delta > address - first) {
			return NULL;
		}
		address -= delta;
	} else {
		delta = (uintptr_t)offset;
		if (delta > limit - address) {
			return NULL;
		}
		address += delta;
	}
	if (address > limit || value_size > limit - address) {
		return NULL;
	}
	return (const void*)address;
}

static PocoStatus po_debug_read_typed_value(const Type_info* type, const void* storage,
											PocoCallbackValue* out_value)
{
	if (po_is_array((Type_info*)type)) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	if (po_is_pointer((Type_info*)type)) {
		memcpy(&out_value->value.popot_value, storage, sizeof(out_value->value.popot_value));
		if (!po_debug_popot_is_bounded(out_value->value.popot_value)) {
			*out_value = po_debug_invalid_value();
			return POCO_STATUS_PARAMETER_RANGE;
		}
		if (out_value->value.popot_value.pt == NULL) {
			out_value->value.popot_value.min = NULL;
			out_value->value.popot_value.max = NULL;
		}
		out_value->kind = POCO_CALLBACK_VALUE_POPOT;
		return POCO_STATUS_OK;
	}
	if (type->comp_count != 1) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	switch (type->comp[0]) {
		case TYPE_CHAR: {
			char stored;
			memcpy(&stored, storage, sizeof(stored));
			out_value->kind = POCO_CALLBACK_VALUE_INT;
			out_value->value.int_value = stored;
			return POCO_STATUS_OK;
		}
		case TYPE_SHORT: {
			short stored;
			memcpy(&stored, storage, sizeof(stored));
			out_value->kind = POCO_CALLBACK_VALUE_INT;
			out_value->value.int_value = stored;
			return POCO_STATUS_OK;
		}
		case TYPE_INT:
			out_value->kind = POCO_CALLBACK_VALUE_INT;
			memcpy(&out_value->value.int_value, storage, sizeof(out_value->value.int_value));
			return POCO_STATUS_OK;
		case TYPE_LONG:
			out_value->kind = POCO_CALLBACK_VALUE_LONG;
			memcpy(&out_value->value.long_value, storage, sizeof(out_value->value.long_value));
			return POCO_STATUS_OK;
		case TYPE_FLOAT: {
			float stored;
			memcpy(&stored, storage, sizeof(stored));
			out_value->kind = POCO_CALLBACK_VALUE_DOUBLE;
			out_value->value.double_value = stored;
			return POCO_STATUS_OK;
		}
		case TYPE_DOUBLE:
			out_value->kind = POCO_CALLBACK_VALUE_DOUBLE;
			memcpy(&out_value->value.double_value, storage, sizeof(out_value->value.double_value));
			return POCO_STATUS_OK;
		default:
			return POCO_STATUS_PARAMETER_RANGE;
	}
}

PocoStatus poco_debug_read_variable(const PocoDebugSession* session, const char* name,
									PocoCallbackValue* out_value)
{
	const PocoDebugLocal* local;
	const void* storage;
	size_t value_size;

	if (session == NULL || name == NULL || out_value == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	*out_value = po_debug_invalid_value();
	if (session->activation == NULL || session->pause_depth == 0 ||
		session->current_frame == NULL || session->current_base == NULL) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	local = po_program_debug_local_lookup(session->current_frame, name, session->current_offset,
										  SHRT_MAX);
	if (local == NULL) {
		return POCO_STATUS_NOT_FOUND;
	}
	if (po_is_array(local->type)) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	if (po_is_pointer(local->type)) {
		value_size = sizeof(Popot);
	} else if (local->type->comp_count != 1) {
		return POCO_STATUS_PARAMETER_RANGE;
	} else {
		switch (local->type->comp[0]) {
			case TYPE_CHAR:
				value_size = sizeof(char);
				break;
			case TYPE_SHORT:
				value_size = sizeof(short);
				break;
			case TYPE_INT:
				value_size = sizeof(int);
				break;
			case TYPE_LONG:
				value_size = sizeof(long);
				break;
			case TYPE_FLOAT:
				value_size = sizeof(float);
				break;
			case TYPE_DOUBLE:
				value_size = sizeof(double);
				break;
			default:
				return POCO_STATUS_PARAMETER_RANGE;
		}
	}
	if (local->storage_scope == SCOPE_LOCAL) {
		storage = po_debug_offset_storage(session->current_base, local->frame_offset,
										  session->current_stack_start, session->current_stack_end,
										  value_size);
	} else if (local->storage_scope == SCOPE_GLOBAL) {
		const PocoActivation* activation = session->activation;
		if (activation->data == NULL || activation->data_size < 0) {
			return POCO_STATUS_PARAMETER_RANGE;
		}
		storage = po_debug_offset_storage(activation->data + activation->data_size,
										  local->frame_offset, activation->data,
										  activation->data + activation->data_size, value_size);
	} else {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	if (storage == NULL) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	return po_debug_read_typed_value(local->type, storage, out_value);
}

static size_t po_debug_collect_backtrace(const PocoDebugSession* session, PocoDebugFrame* frames)
{
	const Func_frame* frame = session->current_frame;
	void* base = session->current_base;
	long line = session->current_line;
	size_t count = 0;
	size_t maximum = session->current_depth + 1;

	while (frame != NULL && count < maximum) {
		if (frames != NULL) {
			frames[count].function = frame->name;
			frames[count].line = line;
		}
		++count;
		if (count == maximum ||
			po_debug_offset_storage(base, 0, session->current_stack_start,
									session->current_stack_end, 2 * sizeof(void*)) == NULL) {
			break;
		}
		{
			void* caller_base;
			Code* return_instruction;
			long return_offset;
			long return_line;

			memcpy(&caller_base, base, sizeof(caller_base));
			memcpy(&return_instruction, (char*)base + sizeof(void*), sizeof(return_instruction));
			frame = po_debug_find_frame(session->activation, return_instruction);
			if (frame == NULL || caller_base == base) {
				break;
			}
			return_offset = return_instruction - frame->code_pt;
			if (!po_debug_line_for_offset(frame->ld, return_offset, &return_line)) {
				break;
			}
			base = caller_base;
			line = return_line;
		}
	}
	return count;
}

PocoStatus poco_debug_backtrace(const PocoDebugSession* session, PocoDebugFrame* frames,
								size_t frame_capacity, size_t* out_frame_count)
{
	size_t required;

	if (session == NULL || out_frame_count == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	*out_frame_count = 0;
	if (session->activation == NULL || session->pause_depth == 0 ||
		session->current_frame == NULL || session->current_base == NULL) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	required = po_debug_collect_backtrace(session, NULL);
	*out_frame_count = required;
	if (frames == NULL || frame_capacity < required) {
		return POCO_STATUS_BUFFER_TOO_SMALL;
	}
	po_debug_collect_backtrace(session, frames);
	return POCO_STATUS_OK;
}

void po_debug_instruction_hook(PocoActivation* activation, Code* instruction, void* stack_area,
							   void* frame_base)
{
	PocoDebugSession* session = activation->debug_session;
	const Func_frame* frame;
	long offset;
	long line;
	int location_changed;
	int stepping_pause;
	int nested_pause;
	const Func_frame* saved_frame;
	long saved_offset;
	long saved_line;
	void* saved_base;
	void* saved_stack_start;
	void* saved_stack_end;
	size_t saved_depth;

	if (session == NULL || session->detach_requested) {
		return;
	}
	frame = po_debug_find_frame(activation, instruction);
	if (frame == NULL) {
		return;
	}
	offset = instruction - frame->code_pt;
	if (!po_debug_line_for_offset(frame->ld, offset, &line)) {
		return;
	}
	location_changed = frame != session->step_frame || line != session->step_line;
	stepping_pause = (session->run_mode == POCO_DEBUG_RUN_STEP_INTO && location_changed) ||
					 (session->run_mode == POCO_DEBUG_RUN_NEXT_OVER && location_changed &&
					  activation->debug_call_depth <= session->step_depth);
	if (!stepping_pause && !po_debug_has_breakpoint(session, frame, offset)) {
		if (session->pause_depth == 0) {
			session->current_frame = frame;
			session->current_offset = offset;
			session->current_line = line;
			session->current_depth = activation->debug_call_depth;
			session->current_base = frame_base;
			session->current_stack_start = stack_area;
			session->current_stack_end = (char*)stack_area + activation->stack_size;
		}
		return;
	}
	nested_pause = session->pause_depth != 0;
	saved_frame = session->current_frame;
	saved_offset = session->current_offset;
	saved_line = session->current_line;
	saved_depth = session->current_depth;
	saved_base = session->current_base;
	saved_stack_start = session->current_stack_start;
	saved_stack_end = session->current_stack_end;
	session->current_frame = frame;
	session->current_offset = offset;
	session->current_line = line;
	session->current_depth = activation->debug_call_depth;
	session->current_base = frame_base;
	session->current_stack_start = stack_area;
	session->current_stack_end = (char*)stack_area + activation->stack_size;
	session->run_mode = POCO_DEBUG_RUN_CONTINUE;
	++session->pause_depth;
	session->pause_callback(session, session->user_data);
	--session->pause_depth;
	if (nested_pause) {
		session->current_frame = saved_frame;
		session->current_offset = saved_offset;
		session->current_line = saved_line;
		session->current_depth = saved_depth;
		session->current_base = saved_base;
		session->current_stack_start = saved_stack_start;
		session->current_stack_end = saved_stack_end;
	}
	if (session->detach_requested && session->pause_depth == 0) {
		po_debug_session_free(session);
	}
}

void po_debug_activation_reset(PocoActivation* activation)
{
	PocoDebugSession* session = activation != NULL ? activation->debug_session : NULL;

	if (session != NULL) {
		session->current_frame = NULL;
		session->current_offset = 0;
		session->current_line = 0;
		session->current_depth = 0;
		session->current_base = NULL;
		session->current_stack_start = NULL;
		session->current_stack_end = NULL;
		session->step_frame = NULL;
		session->step_line = 0;
		session->step_depth = 0;
		session->run_mode = POCO_DEBUG_RUN_CONTINUE;
	}
}

void po_debug_activation_destroy(PocoActivation* activation)
{
	PocoDebugSession* session = activation != NULL ? activation->debug_session : NULL;

	if (session == NULL) {
		return;
	}
	activation->debug_hook = NULL;
	activation->debug_session = NULL;
	session->activation = NULL;
	po_debug_session_free(session);
}
