/*******************************************************************************
 * poco_signature.c - read-only introspection of a compiled function's shape.
 *
 * The VM already carries everything a host needs to check a script's entry
 * points: prototypes record arity, frame kind and return type, and the symbol
 * chain records each parameter's type in declaration order.  Until now that was
 * visible only from inside the call path, which collapses every mismatch into
 * one POCO_STATUS_PARAMETER_RANGE after the host has already committed to the
 * call.  These accessors expose the same data before the call.
 *
 * A struct's identity here is its index in the program's struct table, the same
 * identity the serialized image uses.  Tags are not unique across a program, so
 * the name is reported for diagnostics only.  The index skips entries the image
 * does not table (enums), so an id means the same thing in a freshly compiled
 * program and in one deserialized from a .pex.
 ******************************************************************************/

#include "activation.h"
#include "poco_internal.h"
#include "pocoface.h"
#include "program_internal.h"

#include <string.h>

static const Poco_run_env* po_signature_executable(const PocoProgram* program)
{
	if (program == NULL) {
		return NULL;
	}
	return (const Poco_run_env*)program->executable;
}

/* Struct table entries are the struct and union layouts, in list order. */
static int po_signature_is_tabled(const Struct_info* entry)
{
	return entry != NULL && (entry->type == TYPE_STRUCT || entry->type == TYPE_UNION);
}

static PocoStructId po_signature_struct_id(const Poco_run_env* executable,
										   const Struct_info* wanted)
{
	const Struct_info* entry;
	PocoStructId index = 0;

	if (executable == NULL || wanted == NULL) {
		return POCO_STRUCT_NONE;
	}
	for (entry = executable->struct_infos; entry != NULL; entry = entry->next) {
		if (!po_signature_is_tabled(entry)) {
			continue;
		}
		if (entry == wanted) {
			return index;
		}
		++index;
	}
	return POCO_STRUCT_NONE;
}

static const Struct_info* po_signature_struct_at(const Poco_run_env* executable, PocoStructId id)
{
	const Struct_info* entry;
	PocoStructId index = 0;

	if (executable == NULL || id == POCO_STRUCT_NONE) {
		return NULL;
	}
	for (entry = executable->struct_infos; entry != NULL; entry = entry->next) {
		if (!po_signature_is_tabled(entry)) {
			continue;
		}
		if (index == id) {
			return entry;
		}
		++index;
	}
	return NULL;
}

static size_t po_signature_member_count(const Struct_info* entry)
{
	const Symbol* member;
	size_t count = 0;

	for (member = entry->elements; member != NULL; member = member->next) {
		++count;
	}
	return count;
}

static void po_signature_describe_type(const Poco_run_env* executable, const Type_info* type,
									   PocoTypeDesc* out_type)
{
	unsigned int component;

	memset(out_type, 0, sizeof(*out_type));
	out_type->kind = POCO_CALLBACK_VALUE_INVALID;
	out_type->struct_id = POCO_STRUCT_NONE;
	if (type == NULL) {
		return;
	}
	switch (type->ido_type) {
		case IDO_INT:
			out_type->kind = POCO_CALLBACK_VALUE_INT;
			break;
		case IDO_LONG:
			out_type->kind = POCO_CALLBACK_VALUE_LONG;
			break;
		case IDO_DOUBLE:
			out_type->kind = POCO_CALLBACK_VALUE_DOUBLE;
			break;
		case IDO_POINTER:
			out_type->kind = POCO_CALLBACK_VALUE_POPOT;
			break;
		default:
			break;
	}
	for (component = 0; component < type->comp_count; ++component) {
		const Struct_info* entry;

		if (type->comp[component] != TYPE_STRUCT) {
			continue;
		}
		entry = type->sdims[component].pt;
		if (entry == NULL) {
			continue;
		}
		out_type->is_struct = 1;
		out_type->struct_id = po_signature_struct_id(executable, entry);
		out_type->struct_name = entry->name;
		out_type->struct_size = entry->size > 0 ? (size_t)entry->size : 0;
		out_type->struct_member_count = po_signature_member_count(entry);
		break;
	}
}

static PocoStatus po_signature_find(PocoActivation* activation, const char* name,
									const Func_frame** out_frame,
									const Poco_run_env** out_executable)
{
	const Func_frame* frame;

	if (activation == NULL || name == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (activation->program == NULL || activation->code == NULL) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	frame = po_activation_find_function(activation, name);
	if (frame == NULL) {
		/*
		 * Only Poco functions reach the compiled-function list.  A native -
		 * a registered library's prototype, or a declaration in a
		 * '#pragma poco native' region - has a frame on the prototype chain
		 * and nothing else, so a host asking about one has to be answered
		 * from there.
		 */
		for (frame = activation->code->prototypes; frame != NULL; frame = frame->mlink) {
			if (frame->name != NULL && strcmp(frame->name, name) == 0) {
				break;
			}
		}
	}
	if (frame == NULL) {
		return POCO_STATUS_NOT_FOUND;
	}
	*out_frame = frame;
	*out_executable = po_signature_executable(activation->program);
	return POCO_STATUS_OK;
}

PocoStatus poco_activation_function_signature(PocoActivation* activation, const char* name,
											  PocoFunctionSignature* out_signature)
{
	const Poco_run_env* executable = NULL;
	const Func_frame* frame = NULL;
	PocoStatus status;

	if (out_signature == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	status = po_signature_find(activation, name, &frame, &executable);
	if (status != POCO_STATUS_OK) {
		return status;
	}
	memset(out_signature, 0, sizeof(*out_signature));
	out_signature->name = frame->name;
	out_signature->parameter_count = frame->pcount;
	out_signature->is_native = frame->type == CFF_C;
	po_signature_describe_type(executable, frame->return_type, &out_signature->return_type);
	return POCO_STATUS_OK;
}

PocoStatus poco_activation_function_parameter(PocoActivation* activation, const char* name,
											  size_t index, const char** out_parameter_name,
											  PocoTypeDesc* out_type)
{
	const Poco_run_env* executable = NULL;
	const Func_frame* frame = NULL;
	const Symbol* parameter;
	PocoStatus status;
	size_t position;

	if (out_type == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	status = po_signature_find(activation, name, &frame, &executable);
	if (status != POCO_STATUS_OK) {
		return status;
	}
	if (frame->pcount < 0 || index >= (size_t)frame->pcount) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	parameter = frame->parameters;
	for (position = 0; position < index && parameter != NULL; ++position) {
		parameter = parameter->link;
	}
	if (parameter == NULL) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	po_signature_describe_type(executable, parameter->ti, out_type);
	if (out_parameter_name != NULL) {
		*out_parameter_name = parameter->name;
	}
	return POCO_STATUS_OK;
}

PocoStatus poco_program_struct_member(PocoProgram* program, PocoStructId id, size_t index,
									  const char** out_name, PocoTypeDesc* out_type)
{
	const Poco_run_env* executable;
	const Struct_info* entry;
	const Symbol* member;
	size_t position;

	if (program == NULL || out_type == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	executable = po_signature_executable(program);
	entry = po_signature_struct_at(executable, id);
	if (entry == NULL) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	member = entry->elements;
	for (position = 0; position < index && member != NULL; ++position) {
		member = member->next;
	}
	if (member == NULL) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	po_signature_describe_type(executable, member->ti, out_type);
	if (out_name != NULL) {
		*out_name = member->name;
	}
	return POCO_STATUS_OK;
}
