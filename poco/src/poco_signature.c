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

static uint32_t po_signature_base_type(TypeComp component)
{
	switch (component) {
		case TYPE_VOID:
			return POCO_BASE_TYPE_VOID;
		case TYPE_CHAR:
			return POCO_BASE_TYPE_CHAR;
		case TYPE_UCHAR:
			return POCO_BASE_TYPE_UCHAR;
		case TYPE_SHORT:
			return POCO_BASE_TYPE_SHORT;
		case TYPE_USHORT:
			return POCO_BASE_TYPE_USHORT;
		case TYPE_INT:
			return POCO_BASE_TYPE_INT;
		case TYPE_UINT:
			return POCO_BASE_TYPE_UINT;
		case TYPE_LONG:
			return POCO_BASE_TYPE_LONG;
		case TYPE_ULONG:
			return POCO_BASE_TYPE_ULONG;
		case TYPE_FLOAT:
			return POCO_BASE_TYPE_FLOAT;
		case TYPE_DOUBLE:
			return POCO_BASE_TYPE_DOUBLE;
		case TYPE_STRUCT:
		case TYPE_UNION:
			return POCO_BASE_TYPE_STRUCT;
		case TYPE_END:
		case TYPE_BAD:
			return POCO_BASE_TYPE_INVALID;
		default:
			return POCO_BASE_TYPE_OTHER;
	}
}

/*
 * A Type_info lists its declarators base first: "char **" is CHAR, POINTER,
 * POINTER, and "int (*f)(int)" is INT, FUNCTION, POINTER.  A native pointer
 * may be TYPE_CPT rather than TYPE_POINTER; both are one level of indirection.
 * The image stores this list component for component, so a restored program
 * answers exactly as the program that was serialized.
 */
static void po_signature_shape(const Type_info* type, PocoTypeShape* out_shape)
{
	unsigned int component;

	memset(out_shape, 0, sizeof(*out_shape));
	if (type == NULL || type->comp == NULL || type->comp_count == 0) {
		return;
	}
	out_shape->base_type = po_signature_base_type(type->comp[0]);
	/* The compiler parses "unsigned T" as T and keeps only TFL_UNSIGNED. */
	if ((type->flags & TFL_UNSIGNED) != 0) {
		switch (out_shape->base_type) {
			case POCO_BASE_TYPE_CHAR:
				out_shape->base_type = POCO_BASE_TYPE_UCHAR;
				break;
			case POCO_BASE_TYPE_SHORT:
				out_shape->base_type = POCO_BASE_TYPE_USHORT;
				break;
			case POCO_BASE_TYPE_INT:
				out_shape->base_type = POCO_BASE_TYPE_UINT;
				break;
			case POCO_BASE_TYPE_LONG:
				out_shape->base_type = POCO_BASE_TYPE_ULONG;
				break;
			default:
				break;
		}
	}
	for (component = 1; component < type->comp_count; ++component) {
		switch (type->comp[component]) {
			case TYPE_POINTER:
			case TYPE_CPT:
				++out_shape->pointer_depth;
				break;
			case TYPE_ARRAY:
				++out_shape->array_rank;
				break;
			case TYPE_FUNCTION:
			case TYPE_CFUNCTION:
				out_shape->is_function = 1;
				break;
			default:
				break;
		}
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

static PocoStatus po_signature_parameter(PocoActivation* activation, const char* name,
										 size_t index, const Symbol** out_parameter,
										 const Poco_run_env** out_executable)
{
	const Func_frame* frame = NULL;
	const Symbol* parameter;
	PocoStatus status;
	size_t position;

	status = po_signature_find(activation, name, &frame, out_executable);
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
	*out_parameter = parameter;
	return POCO_STATUS_OK;
}

PocoStatus poco_activation_function_parameter(PocoActivation* activation, const char* name,
											  size_t index, const char** out_parameter_name,
											  PocoTypeDesc* out_type)
{
	const Poco_run_env* executable = NULL;
	const Symbol* parameter = NULL;
	PocoStatus status;

	if (out_type == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	status = po_signature_parameter(activation, name, index, &parameter, &executable);
	if (status != POCO_STATUS_OK) {
		return status;
	}
	po_signature_describe_type(executable, parameter->ti, out_type);
	if (out_parameter_name != NULL) {
		*out_parameter_name = parameter->name;
	}
	return POCO_STATUS_OK;
}

static PocoStatus po_signature_member(PocoProgram* program, PocoStructId id, size_t index,
									  const Symbol** out_member,
									  const Poco_run_env** out_executable)
{
	const Poco_run_env* executable;
	const Struct_info* entry;
	const Symbol* member;
	size_t position;

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
	*out_member = member;
	*out_executable = executable;
	return POCO_STATUS_OK;
}

PocoStatus poco_program_struct_member(PocoProgram* program, PocoStructId id, size_t index,
									  const char** out_name, PocoTypeDesc* out_type)
{
	const Poco_run_env* executable = NULL;
	const Symbol* member = NULL;
	PocoStatus status;

	if (program == NULL || out_type == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	status = po_signature_member(program, id, index, &member, &executable);
	if (status != POCO_STATUS_OK) {
		return status;
	}
	po_signature_describe_type(executable, member->ti, out_type);
	if (out_name != NULL) {
		*out_name = member->name;
	}
	return POCO_STATUS_OK;
}

PocoStatus poco_activation_function_return_shape(PocoActivation* activation, const char* name,
												 PocoTypeShape* out_shape)
{
	const Poco_run_env* executable = NULL;
	const Func_frame* frame = NULL;
	PocoStatus status;

	if (out_shape == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	status = po_signature_find(activation, name, &frame, &executable);
	if (status != POCO_STATUS_OK) {
		return status;
	}
	po_signature_shape(frame->return_type, out_shape);
	return POCO_STATUS_OK;
}

PocoStatus poco_activation_function_parameter_shape(PocoActivation* activation, const char* name,
													size_t index, PocoTypeShape* out_shape)
{
	const Poco_run_env* executable = NULL;
	const Symbol* parameter = NULL;
	PocoStatus status;

	if (out_shape == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	status = po_signature_parameter(activation, name, index, &parameter, &executable);
	if (status != POCO_STATUS_OK) {
		return status;
	}
	po_signature_shape(parameter->ti, out_shape);
	return POCO_STATUS_OK;
}

PocoStatus poco_program_struct_member_shape(PocoProgram* program, PocoStructId id, size_t index,
											PocoTypeShape* out_shape)
{
	const Poco_run_env* executable = NULL;
	const Symbol* member = NULL;
	PocoStatus status;

	if (program == NULL || out_shape == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	status = po_signature_member(program, id, index, &member, &executable);
	if (status != POCO_STATUS_OK) {
		return status;
	}
	po_signature_shape(member->ti, out_shape);
	return POCO_STATUS_OK;
}
