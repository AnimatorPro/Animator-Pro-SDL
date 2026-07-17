/****************************************************************************
 * poco_ffi.c - replacement for runcall.asm, using libffi to pass parameters
 *              to compiled functions based on parsed Func_frame objects.
 *
 * I went this direction because the runcall.asm solution is brilliant, but
 * I was hoping to keep the project as cross-platform as possible.  Libffi
 * looks like the best bet and is in use by some of the world's biggest
 * projects.
 *
 * MAINTENANCE:
 *	28/dec/2022    (kiki)   File created, and structs added to poco.h.
 *	19/nov/2023    (kiki)   Finally managed to get a variadic call to printf
 *							working through libffi! Also simplified some of the
 *							structures.
 *
 ***************************************************************************/

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <hashmap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "poco.h"
#include "pocoface.h"
#include "pocolib.h"
#include "ptrmacro.h"

#ifdef _MSC_VER
#include <float.h>
#endif

typedef HASHMAP(void, Po_FFI) _func_hash_map;
typedef HASHMAP(char, void) _name_hash_map;

struct po_func_map {
	_func_hash_map map;
	_name_hash_map name_map;
};

typedef struct poco_pointer_span
{
	struct poco_pointer_span* next;
	void* pointer;
	size_t byte_count;
	uint32_t permissions;
	PocoOwnedPointerRelease release;
	void* release_user_data;
	bool active;
	bool owned;
} PocoPointerSpan;

struct poco_pointer_registry
{
	PocoPointerSpan* spans;
};

size_t po_ffi_funcmap_hash(const void* data);
int po_ffi_func_compare(const void* left, const void* right);
static const char* po_ffi_status_str(const ffi_status status);

static bool poco_pointer_span_range(const void* pointer, size_t byte_count,
	uintptr_t* out_start, uintptr_t* out_end)
{
	uintptr_t start;

	if (pointer == NULL || byte_count == 0 || out_start == NULL || out_end == NULL)
		return false;
	start = (uintptr_t)pointer;
	if (byte_count - 1 > UINTPTR_MAX - start)
		return false;
	*out_start = start;
	*out_end = start + byte_count - 1;
	return true;
}

static PocoPointerSpan* poco_pointer_registry_find_span(PocoPointerRegistry* registry,
	const void* pointer, size_t byte_count, bool* out_stale)
{
	PocoPointerSpan* span;
	PocoPointerSpan* stale = NULL;
	uintptr_t pointer_start;
	uintptr_t pointer_end;

	if (out_stale != NULL)
		*out_stale = false;
	if (registry == NULL || !poco_pointer_span_range(pointer, byte_count,
		&pointer_start, &pointer_end)) {
		return NULL;
	}
	for (span = registry->spans; span != NULL; span = span->next) {
		uintptr_t span_start;
		uintptr_t span_end;

		if (!poco_pointer_span_range(span->pointer, span->byte_count,
			&span_start, &span_end) || pointer_start < span_start ||
			pointer_end > span_end) {
			continue;
		}
		if (span->active)
			return span;
		stale = span;
	}
	if (out_stale != NULL && stale != NULL)
		*out_stale = true;
	return NULL;
}

PocoPointerRegistry* poco_pointer_registry_create(void)
{
	return calloc(1, sizeof(PocoPointerRegistry));
}

void poco_pointer_registry_destroy(PocoPointerRegistry* registry)
{
	PocoPointerSpan* span;
	PocoPointerSpan* next;

	if (registry == NULL)
		return;
	for (span = registry->spans; span != NULL; span = next) {
		next = span->next;
		if (span->active && span->owned && span->pointer != NULL) {
			if (span->release != NULL)
				span->release(span->pointer, span->release_user_data);
			else
				free(span->pointer);
		}
		free(span);
	}
	free(registry);
}

static Errcode poco_pointer_registry_register(PocoPointerRegistry* registry,
	void* pointer, size_t byte_count, uint32_t permissions, bool owned,
	PocoOwnedPointerRelease release, void* release_user_data)
{
	PocoPointerSpan* span;
	uintptr_t start;
	uintptr_t end;

	if (registry == NULL || !poco_pointer_span_range(pointer, byte_count, &start, &end) ||
		(permissions & (POCO_POINTER_PERMISSION_READ | POCO_POINTER_PERMISSION_WRITE)) == 0) {
		return Err_parameter_range;
	}
	(void)start;
	(void)end;
	for (span = registry->spans; span != NULL; span = span->next) {
		if (span->pointer == pointer) {
			if (span->active && span->owned != owned)
				return Err_parameter_range;
			span->byte_count = byte_count;
			span->permissions = permissions;
			span->owned = owned;
			span->release = release;
			span->release_user_data = release_user_data;
			span->active = true;
			return Success;
		}
	}
	span = calloc(1, sizeof(*span));
	if (span == NULL)
		return Err_no_memory;
	span->pointer = pointer;
	span->byte_count = byte_count;
	span->permissions = permissions;
	span->owned = owned;
	span->release = release;
	span->release_user_data = release_user_data;
	span->active = true;
	span->next = registry->spans;
	registry->spans = span;
	return Success;
}

Errcode poco_pointer_registry_register_borrowed(PocoPointerRegistry* registry,
	void* pointer, size_t byte_count, uint32_t permissions)
{
	return poco_pointer_registry_register(registry, pointer, byte_count, permissions,
		false, NULL, NULL);
}

Errcode poco_pointer_registry_unregister_borrowed(PocoPointerRegistry* registry,
	const void* pointer)
{
	PocoPointerSpan* span;

	if (registry == NULL || pointer == NULL)
		return Err_parameter_range;
	for (span = registry->spans; span != NULL; span = span->next) {
		if (span->pointer == pointer && !span->owned) {
			if (!span->active)
				return Err_not_found;
			span->active = false;
			return Success;
		}
	}
	return Err_not_found;
}

Errcode poco_pointer_registry_register_owned(PocoPointerRegistry* registry,
	void* pointer, size_t byte_count, uint32_t permissions,
	PocoOwnedPointerRelease release, void* release_user_data)
{
	return poco_pointer_registry_register(registry, pointer, byte_count, permissions,
		true, release, release_user_data);
}

Errcode poco_pointer_registry_release_owned(PocoPointerRegistry* registry,
	const void* pointer)
{
	PocoPointerSpan* span;

	if (registry == NULL || pointer == NULL)
		return Err_parameter_range;
	for (span = registry->spans; span != NULL; span = span->next) {
		if (span->pointer == pointer && span->owned) {
			if (!span->active)
				return Err_not_found;
			span->active = false;
			return Success;
		}
	}
	return Err_not_found;
}

bool poco_pointer_registry_validate(PocoPointerRegistry* registry,
	const Popot* pointer, size_t byte_count, uint32_t permissions)
{
	PocoPointerSpan* span;
	uintptr_t minimum;
	uintptr_t current;
	uintptr_t maximum;
	uintptr_t required_end;
	bool stale;

	if (byte_count == 0)
		return true;
	if (pointer == NULL || pointer->pt == NULL || pointer->min == NULL || pointer->max == NULL)
		return false;
	minimum = (uintptr_t)pointer->min;
	current = (uintptr_t)pointer->pt;
	maximum = (uintptr_t)pointer->max;
	if (minimum > current || current > maximum || byte_count - 1 > UINTPTR_MAX - current)
		return false;
	required_end = current + byte_count - 1;
	if (required_end > maximum)
		return false;
	span = poco_pointer_registry_find_span(registry, pointer->pt, byte_count, &stale);
	if (span == NULL)
		return !stale;
	return (span->permissions & permissions) == permissions;
}

bool poco_pointer_registry_find(PocoPointerRegistry* registry,
	const void* pointer, Popot* out_pointer, uint32_t* out_permissions)
{
	PocoPointerSpan* span;
	uintptr_t start;
	uintptr_t end;
	bool stale;

	if (out_pointer == NULL || !poco_pointer_span_range(pointer, 1, &start, &end))
		return false;
	span = poco_pointer_registry_find_span(registry, pointer, 1, &stale);
	if (span == NULL)
		return false;
	if (!poco_pointer_span_range(span->pointer, span->byte_count, &start, &end))
		return false;
	out_pointer->pt = (void*)pointer;
	out_pointer->min = span->pointer;
	out_pointer->max = (void*)end;
	if (out_permissions != NULL)
		*out_permissions = span->permissions;
	return true;
}

// ===============================================================
/*
 * Convert a poco type to an FFI type.
 */
ffi_type* po_ffi_type_from_ido_type(IdoType ido_type)
{
	//! TODO: Handle sub types?

	switch (ido_type) {
		case IDO_INT:
			return &ffi_type_sint;
			break;
		case IDO_LONG:
			return &ffi_type_slong;
			break;
		case IDO_DOUBLE:
			return &ffi_type_double;
			break;

			/* kiki note:
			 * the old pointers were much smaller; I'm not sure that
			 * there is really a difference between IDO_POINTER and
			 * IDO_CPT anymore. */

#ifdef STRING_EXPERIMENT
		case IDO_STRING:
#endif

		case IDO_POINTER:
		case IDO_CPT:
			return &ffi_type_pointer;
			break;

		case IDO_VOID:
			return &ffi_type_void;

		default:
			return NULL;
	}
}

// ===============================================================
/*
 * Return a string name for a specified FFI type.
 */

const char* po_ffi_name_for_type(const ffi_type* type)
{
	/* Can't switch on a pointer, but this is fine for now. */
	if (type == &ffi_type_sint) {
		return "int";
	}
	if (type == &ffi_type_uint) {
		return "unsigned int";
	} else if (type == &ffi_type_uint32) {
		return "unsigned long";
	} else if (type == &ffi_type_sint32) {
		return "sint32";
	} else if (type == &ffi_type_slong) {
		return "long";
	} else if (type == &ffi_type_double) {
		return "double";
	} else if (type == &ffi_type_pointer) {
		return "pointer";
	} else if (type == &ffi_type_void) {
		return "void";
	} else if (type == &ffi_type_uint8) {
		return "uint8";
	} else if (type == &ffi_type_sint8) {
		return "sint8";
	} else if (type == &ffi_type_uint16) {
		return "uint16";
	} else if (type == &ffi_type_sint16) {
		return "sint16";
	} else if (type == &ffi_type_uint64) {
		return "uint64";
	} else if (type == &ffi_type_sint64) {
		return "sint64";
	} else if (type == &ffi_type_float) {
		return "float";
	}
	return "unknown type";
}

// ===============================================================
/*
 * Return the size of the specified FFI type in bytes
 */
static size_t po_ffi_ido_type_size(IdoType ido_type)
{
	switch (ido_type) {
		case IDO_INT:
			return sizeof(int);
		case IDO_LONG:
			return sizeof(long);
		case IDO_DOUBLE:
			return sizeof(double);

#ifdef STRING_EXPERIMENT
		case IDO_STRING:
#endif
			/* kiki note:
			 * the old pointers were much smaller; I'm not sure that
			 * there is really a difference between IDO_POINTER and
			 * IDO_CPT anymore. */

		case IDO_POINTER:
			return sizeof(Popot);
		case IDO_CPT:
			return sizeof(void*);

		default:
			return 0;
	}
}

// ===============================================================
bool po_ffi_is_variadic(const Po_FFI* binding)
{
	return binding->flags & PO_FFI_VARIADIC;
}

static bool po_ffi_contract_index_is_scalar(const Po_FFI* binding, size_t index)
{
	return index < binding->arg_count &&
		(binding->arg_ido_types[index] == IDO_INT ||
		 binding->arg_ido_types[index] == IDO_LONG);
}

static bool po_ffi_contract_index_is_pointer(const Po_FFI* binding, size_t index)
{
	return index < binding->arg_count && binding->arg_ido_types[index] == IDO_POINTER;
}

static uint32_t po_ffi_pointer_depth(const Type_info* type)
{
	uint32_t depth = 0;
	int index;

	if (type == NULL)
		return 0;
	for (index = 0; index < type->comp_count; ++index) {
		if (type->comp[index] == TYPE_POINTER || type->comp[index] == TYPE_CPT ||
			type->comp[index] == TYPE_ARRAY) {
			++depth;
		}
	}
	return depth;
}

static bool po_ffi_contract_size_index_is_valid(const Po_FFI* binding, size_t index)
{
	return index == POCO_BINDING_PARAMETER_NONE ||
		po_ffi_contract_index_is_scalar(binding, index);
}

static bool po_ffi_contract_is_valid(const Po_FFI* binding)
{
	const PocoBindingContract* contract = binding->contract;
	size_t index;

	if (contract == NULL)
		return true;
	if (contract->pointer_contract_count != 0 && contract->pointer_contracts == NULL)
		return false;
	for (index = 0; index < contract->pointer_contract_count; ++index) {
		const PocoBindingPointerContract* pointer = &contract->pointer_contracts[index];

		if (!po_ffi_contract_index_is_pointer(binding, pointer->parameter_index) ||
			pointer->pointer_depth == 0 ||
			binding->arg_pointer_depths[pointer->parameter_index] != pointer->pointer_depth ||
			(pointer->permissions & (POCO_POINTER_PERMISSION_READ |
				POCO_POINTER_PERMISSION_WRITE)) == 0 ||
			pointer->span_kind > POCO_BINDING_SPAN_APPEND_C_STRING ||
			!po_ffi_contract_size_index_is_valid(binding, pointer->byte_count_parameter) ||
			!po_ffi_contract_size_index_is_valid(binding, pointer->element_count_parameter) ||
			(pointer->string_parameter != POCO_BINDING_PARAMETER_NONE &&
			 !po_ffi_contract_index_is_pointer(binding, pointer->string_parameter))) {
			return false;
		}
		if (pointer->span_kind != POCO_BINDING_SPAN_BYTES &&
			pointer->string_parameter == POCO_BINDING_PARAMETER_NONE &&
			pointer->span_kind != POCO_BINDING_SPAN_APPEND_C_STRING) {
			/* A string span without an explicit source scans its own pointer. */
			continue;
		}
	}
	if (contract->return_value.origin != POCO_POINTER_RETURN_TRUSTED) {
		if (contract->return_value.origin > POCO_POINTER_RETURN_BORROWED ||
			binding->result_ido_type != IDO_POINTER) {
			return false;
		}
		if (contract->return_value.origin == POCO_POINTER_RETURN_ALIAS &&
			!po_ffi_contract_index_is_pointer(binding,
				contract->return_value.alias_parameter)) {
			return false;
		}
		if (contract->return_value.origin == POCO_POINTER_RETURN_OWNED &&
			(contract->return_value.span_kind > POCO_BINDING_SPAN_APPEND_C_STRING ||
			 !po_ffi_contract_size_index_is_valid(binding,
				contract->return_value.byte_count_parameter) ||
			 !po_ffi_contract_size_index_is_valid(binding,
				contract->return_value.element_count_parameter) ||
			 (contract->return_value.string_parameter != POCO_BINDING_PARAMETER_NONE &&
			  !po_ffi_contract_index_is_pointer(binding,
				contract->return_value.string_parameter)))) {
			return false;
		}
	}
	return true;
}

// ===============================================================
/*
 * Validate and create a descriptor before it is visible through either
 * function map.  Its fixed native argument metadata is dynamically sized;
 * values and any variadic argument metadata are owned by individual calls.
 */
static Errcode po_ffi_create_binding(const C_frame* frame, Po_FFI** out_binding)
{
	Po_FFI* binding;
	Symbol* param;
	unsigned int fixed_count = 0;
	unsigned int index;
	bool is_variadic = false;
	ffi_type* type;
	ffi_status ffi_status;

	if (out_binding == NULL)
		return Err_poco_ffi_invalid_binding;
	*out_binding = NULL;
	if (frame == NULL || frame->name == NULL || frame->name[0] == '\0') {
		poco_set_error("FFI binding has no name.");
		return Err_poco_ffi_invalid_binding;
	}
	if (frame->code_pt == NULL) {
		poco_set_error("FFI binding '%s' has a null function.", frame->name);
		return Err_poco_ffi_invalid_binding;
	}
	if (frame->pcount < 0) {
		poco_set_error("FFI binding '%s' has an invalid parameter count.", frame->name);
		return Err_poco_ffi_invalid_binding;
	}

	param = frame->parameters;
	for (index = 0; index < (unsigned int)frame->pcount; ++index) {
		if (param == NULL || param->ti == NULL) {
			poco_set_error("FFI binding '%s' has incomplete parameter metadata.", frame->name);
			return Err_poco_ffi_invalid_binding;
		}
		if (param->ti->comp_count != 0 && param->ti->comp == NULL) {
			poco_set_error("FFI binding '%s' has incomplete parameter type metadata.", frame->name);
			return Err_poco_ffi_invalid_binding;
		}
		if (param->ti->comp_count != 0 && param->ti->comp[0] == TYPE_ELLIPSIS) {
			if (is_variadic || index + 1 != (unsigned int)frame->pcount) {
				poco_set_error("FFI binding '%s' has an invalid variadic declaration.", frame->name);
				return Err_poco_ffi_invalid_binding;
			}
			is_variadic = true;
		} else {
			type = po_ffi_type_from_ido_type(param->ti->ido_type);
			if (type == NULL || type == &ffi_type_void) {
				poco_set_error("FFI binding '%s' has an unsupported parameter type.", frame->name);
				return Err_poco_ffi_invalid_binding;
			}
			++fixed_count;
		}
		param = param->link;
	}
	if (param != NULL) {
		poco_set_error("FFI binding '%s' has inconsistent parameter metadata.", frame->name);
		return Err_poco_ffi_invalid_binding;
	}
	if (is_variadic && fixed_count == 0) {
		poco_set_error("FFI binding '%s' has no fixed parameter before '...'.", frame->name);
		return Err_poco_ffi_invalid_binding;
	}

	type = frame->return_type == NULL ? &ffi_type_void :
		po_ffi_type_from_ido_type(frame->return_type->ido_type);
	if (type == NULL) {
		poco_set_error("FFI binding '%s' has an unsupported return type.", frame->name);
		return Err_poco_ffi_invalid_binding;
	}

	binding = calloc(1, sizeof(*binding));
	if (binding == NULL) {
		poco_set_error("Unable to allocate FFI binding '%s'.", frame->name);
		return Err_no_memory;
	}
	binding->name = frame->name;
	binding->function = frame->code_pt;
	binding->contract = frame->binding_contract;
	binding->arg_count = fixed_count;
	binding->result_ido_type = frame->return_type == NULL ? IDO_VOID : frame->return_type->ido_type;
	binding->result_type = type;
	binding->arg_types = calloc((size_t)fixed_count + 1, sizeof(*binding->arg_types));
	binding->arg_ido_types = calloc((size_t)fixed_count + 1, sizeof(*binding->arg_ido_types));
	binding->arg_pointer_depths = calloc((size_t)fixed_count + 1,
		sizeof(*binding->arg_pointer_depths));
	if (binding->arg_types == NULL || binding->arg_ido_types == NULL ||
		binding->arg_pointer_depths == NULL) {
		poco_set_error("Unable to allocate FFI argument metadata for '%s'.", frame->name);
		po_ffi_delete(binding);
		return Err_no_memory;
	}

	if (is_variadic) {
		binding->flags |= PO_FFI_VARIADIC;
	}

	param = frame->parameters;
	index = 0;
	while (param != NULL) {
		if (param->ti->comp_count == 0 || param->ti->comp[0] != TYPE_ELLIPSIS) {
			binding->arg_ido_types[index] = param->ti->ido_type;
			binding->arg_types[index] = po_ffi_type_from_ido_type(param->ti->ido_type);
			binding->arg_pointer_depths[index] = po_ffi_pointer_depth(param->ti);
			++index;
		}
		param = param->link;
	}

	if (is_variadic)
		ffi_status = ffi_prep_cif_var(&binding->interface, FFI_DEFAULT_ABI,
			binding->arg_count, binding->arg_count, binding->result_type, binding->arg_types);
	else
		ffi_status = ffi_prep_cif(&binding->interface, FFI_DEFAULT_ABI,
			binding->arg_count, binding->result_type, binding->arg_types);
	if (ffi_status != FFI_OK) {
		poco_set_error("FFI binding '%s' could not prepare its call interface: %s.",
			binding->name, po_ffi_status_str(ffi_status));
		po_ffi_delete(binding);
		return Err_poco_ffi_invalid_binding;
	}
	if (!po_ffi_contract_is_valid(binding)) {
		poco_set_error("FFI binding '%s' has an invalid pointer contract (result %d, args %u).",
			binding->name, binding->result_ido_type, binding->arg_count);
		po_ffi_delete(binding);
		return Err_poco_ffi_invalid_binding;
	}

	*out_binding = binding;
	return Success;
}

/* Retained for internal callers that only need the descriptor pointer. */
Po_FFI* po_ffi_new(const C_frame* frame)
{
	Po_FFI* binding = NULL;

	(void)po_ffi_create_binding(frame, &binding);
	return binding;
}

// ===============================================================
static const char* po_ffi_status_str(const ffi_status status)
{
	switch (status) {
		case FFI_OK:
			return "OK";
		case FFI_BAD_TYPEDEF:
			return "Bad Typedef";
		case FFI_BAD_ABI:
			return "Bad ABI";
		case FFI_BAD_ARGTYPE:
			return "Bad ArgType";
	}
	return "Unknown libffi status";
}

// ===============================================================
static inline char* po_ffi_argtype_str(const ffi_type* type)
{
	if (type == &ffi_type_void) {
		return "ffi_type_void";
	}
	if (type == &ffi_type_uint8) {
		return "ffi_type_uint8";
	}
	if (type == &ffi_type_sint8) {
		return "ffi_type_sint8";
	}
	if (type == &ffi_type_uint16) {
		return "ffi_type_uint16";
	}
	if (type == &ffi_type_sint16) {
		return "ffi_type_sint16";
	}
	if (type == &ffi_type_uint32) {
		return "ffi_type_uint32";
	}
	if (type == &ffi_type_sint32) {
		return "ffi_type_sint32";
	}
	if (type == &ffi_type_slong) {
		return "ffi_type_slong";
	}
	if (type == &ffi_type_uint64) {
		return "ffi_type_uint64";
	}
	if (type == &ffi_type_sint64) {
		return "ffi_type_sint64";
	}
	if (type == &ffi_type_float) {
		return "ffi_type_float";
	}
	if (type == &ffi_type_double) {
		return "ffi_type_double";
	}
	if (type == &ffi_type_pointer) {
		return "ffi_type_pointer";
	}

	return "";
}

// ===============================================================
static size_t po_ffi_argtype_size(const ffi_type* type)
{
	if (type == &ffi_type_void) {
		return 0;
	}
	if (type == &ffi_type_uint8) {
		return sizeof(uint8_t);
	}
	if (type == &ffi_type_sint8) {
		return sizeof(int8_t);
	}
	if (type == &ffi_type_uint16) {
		return sizeof(uint16_t);
	}
	if (type == &ffi_type_sint16) {
		return sizeof(int16_t);
	}
	if (type == &ffi_type_uint32) {
		return sizeof(uint32_t);
	}
	if (type == &ffi_type_sint32) {
		return sizeof(int32_t);
	}
	if (type == &ffi_type_slong) {
		return sizeof(long);
	}
	if (type == &ffi_type_uint64) {
		return sizeof(uint64_t);
	}
	if (type == &ffi_type_sint64) {
		return sizeof(int64_t);
	}
	if (type == &ffi_type_float) {
		return sizeof(float);
	}
	if (type == &ffi_type_double) {
		return sizeof(double);
	}
	if (type == &ffi_type_pointer) {
		return sizeof(void*);
	}

	return 0;
}

static bool po_ffi_variadic_type_needs_promotion(const ffi_type* type)
{
	if (type == &ffi_type_float)
		return true;
	if (sizeof(int) > sizeof(int8_t) &&
		(type == &ffi_type_uint8 || type == &ffi_type_sint8))
		return true;
	if (sizeof(int) > sizeof(int16_t) &&
		(type == &ffi_type_uint16 || type == &ffi_type_sint16))
		return true;
	return false;
}

void po_ffi_variadic_types_reset(Po_FFI_Variadic_Descriptor* variadic)
{
	if (variadic != NULL)
		variadic->count = 0;
}

void po_ffi_variadic_types_release(Po_FFI_Variadic_Descriptor* variadic)
{
	if (variadic == NULL)
		return;
	free(variadic->types);
	memset(variadic, 0, sizeof(*variadic));
}

Errcode po_ffi_variadic_types_append(Po_FFI_Variadic_Descriptor* variadic,
	ffi_type* type)
{
	unsigned int new_capacity;
	ffi_type** new_types;

	if (variadic == NULL || type == NULL || type == &ffi_type_void ||
		po_ffi_variadic_type_needs_promotion(type) ||
		po_ffi_argtype_size(type) == 0) {
		poco_set_error("Unsupported variadic FFI argument type '%s'.",
			po_ffi_name_for_type(type));
		return Err_poco_ffi_invalid_binding;
	}
	if (variadic->count == UINT_MAX)
		return Err_poco_ffi_variadic_overflow;
	if (variadic->count == variadic->capacity) {
		if (variadic->capacity == 0)
			new_capacity = 8;
		else if (variadic->capacity > UINT_MAX / 2)
			return Err_poco_ffi_variadic_overflow;
		else
			new_capacity = variadic->capacity * 2;
		if ((size_t)new_capacity > SIZE_MAX / sizeof(*variadic->types))
			return Err_poco_ffi_variadic_overflow;
		new_types = realloc(variadic->types,
			(size_t)new_capacity * sizeof(*variadic->types));
		if (new_types == NULL)
			return Err_no_memory;
		variadic->types = new_types;
		variadic->capacity = new_capacity;
	}
	variadic->types[variadic->count++] = type;
	return Success;
}

// ===============================================================
/* Fully deallocate a Po_FFI struct pointer created by po_ffi_new. */
void po_ffi_delete(Po_FFI* binding)
{
	if (binding == NULL)
		return;
	free(binding->arg_types);
	free(binding->arg_ido_types);
	free(binding->arg_pointer_depths);
	free(binding);
}

// ===============================================================
/*
 * Quick hash for function pointers, and a comparator.
 */

size_t po_ffi_funcmap_hash(const void* data)
{
	return hashmap_hash_default(data, sizeof(void*));
}

int po_ffi_func_compare(const void* left, const void* right)
{
	if (left == right) {
		return 0;
	} else {
		return 1;
	}
}

// ===============================================================
/*
 * Create a new hashmap for functions with the wrapper struct.
 */
static Po_FuncMap* po_ffi_funcmap_new()
{
	Po_FuncMap* result = malloc(sizeof(Po_FuncMap));
	if (!result) {
		return NULL;
	}

	hashmap_init(&result->map, po_ffi_funcmap_hash, po_ffi_func_compare);
	hashmap_init(&result->name_map, hashmap_hash_string_i, strcmp);
	return result;
}

// ===============================================================
/*
 * Destroys a function hashmap and deallocates its data.
 */
static void po_ffi_funcmap_delete(Po_FuncMap* func_map)
{
	Po_FFI* binding = NULL;
	void* key = NULL;

	if (func_map == NULL)
		return;

	hashmap_foreach(key, binding, &func_map->map)
	{
		(void)key;
		po_ffi_delete(binding);
	}

	hashmap_cleanup(&func_map->map);
	hashmap_cleanup(&func_map->name_map);
	free(func_map);
}

void po_ffi_free_structures(Poco_run_env* env)
{
	if (env == NULL)
		return;
	po_ffi_variadic_types_release(&env->variadic);
	if (env->func_map == NULL)
		return;
	po_ffi_funcmap_delete(env->func_map);
	env->func_map = NULL;
}

// ===============================================================
/*
 * 	Provided po_ffi_build_structures has been run on the environment,
 *	try to find and return the function specified by the pointer key.
 *
 *	Returns NULL if the function is not in the map.
 */
Po_FFI* po_ffi_find_binding(const Poco_run_env* env, const void* key)
{
	if (!env->func_map) {
		fprintf(stderr, "%s: called on env with NULL map.\n", __FUNCTION__);
		return NULL;
	}

	Po_FFI* binding = hashmap_get(&env->func_map->map, key);
	if (!binding) {
		builtin_err = Err_poco_ffi_func_not_found;
	}
	return binding;
}

/*
 * 	Look up the function binding by name instead of func pointer.
 *
 *	Returns NULL if the function is not in the map.
 */
Po_FFI* po_ffi_find_binding_by_name(const Poco_run_env* env, const char* name)
{
	if (!env->func_map) {
		fprintf(stderr, "%s: called on env with NULL map.\n", __FUNCTION__);
		return NULL;
	}

	void* key = (void*)hashmap_get(&env->func_map->name_map, name);
	if (!key) {
		return NULL;
	}

	Po_FFI* binding = (Po_FFI*)hashmap_get(&env->func_map->map, key);
	return binding;
}

// ===============================================================
/*
	Take the compiled run environment and build the libffi strucures
	required for function calls.
*/

int po_ffi_build_structures(Poco_run_env* env)
{
	Errcode err;
	int put_result;
	Po_FFI* binding;
	C_frame* frame;

	if (!env || !env->protos || !env->protos->next || !env->protos->next->mlink) {
		return Err_poco_ffi_no_protos;
	}
	if (env->func_map != NULL) {
		poco_set_error("FFI structures have already been built for this program.");
		return Err_poco_ffi_invalid_binding;
	}

	/* Near as I can tell:
	 * - env->protos points to a Func_frame for the whole file
	   - env->protos->next point to the main function
	   - env->protos->mlink is the same as env->protos->next
	   - env->protos->next->mlink is the start of the library functions
	*/

	/* allocate funcmap */
	env->func_map = po_ffi_funcmap_new();
	if (!env->func_map) {
		return Err_poco_ffi_no_func_map;
	}

	frame = env->protos;

	while (frame->mlink) {
		frame = frame->mlink;
		if (frame->type != CFF_C) {
			continue;
		}

		err = po_ffi_create_binding(frame, &binding);
		if (err != Success)
			goto ERROR;
		if (hashmap_get(&env->func_map->name_map, binding->name) != NULL) {
			poco_set_error("FFI binding '%s' duplicates an existing function name.", binding->name);
			po_ffi_delete(binding);
			err = Err_poco_ffi_invalid_binding;
			goto ERROR;
		}
		if (hashmap_get(&env->func_map->map, binding->function) != NULL) {
			poco_set_error("FFI binding '%s' duplicates an existing function address.", binding->name);
			po_ffi_delete(binding);
			err = Err_poco_ffi_invalid_binding;
			goto ERROR;
		}

		put_result = hashmap_put(&env->func_map->name_map, binding->name, binding->function);
		if (put_result < 0) {
			poco_set_error("Unable to add FFI binding '%s' to the name map.", binding->name);
			po_ffi_delete(binding);
			err = Err_poco_ffi_no_map_insert;
			goto ERROR;
		}
		put_result = hashmap_put(&env->func_map->map, binding->function, binding);
		if (put_result < 0) {
			hashmap_remove(&env->func_map->name_map, binding->name);
			poco_set_error("Unable to add FFI binding '%s' to the address map.", binding->name);
			po_ffi_delete(binding);
			err = Err_poco_ffi_no_map_insert;
			goto ERROR;
		}
	}

	return Success;

ERROR:
	po_ffi_free_structures(env);
	return err;
}

// ===============================================================
/* Per-call values have to stay separate from an immutable binding descriptor:
 * libffi retains neither the argument pointer array nor its pointed-to data. */
typedef struct po_ffi_call
{
	ffi_cif interface;
	unsigned int arg_count;
	void** args;
	ffi_type** arg_types;
	Po_FFI_Data* data;
	Popot* pointer_arguments;
	Po_FFI_Data result;
} Po_FFI_Call;

static void po_ffi_call_release(Po_FFI_Call* call)
{
	free(call->args);
	free(call->arg_types);
	free(call->data);
	free(call->pointer_arguments);
	memset(call, 0, sizeof(*call));
}

static bool po_ffi_call_allocate(Po_FFI_Call* call, unsigned int arg_count)
{
	const size_t count = (size_t)arg_count;
	size_t slots;

	if (count == SIZE_MAX)
		return false;
	slots = count + 1; /* retain a checked trailing NULL slot for libffi. */
	if (slots > SIZE_MAX / sizeof(*call->args) ||
		slots > SIZE_MAX / sizeof(*call->arg_types) ||
		slots > SIZE_MAX / sizeof(*call->data) ||
		slots > SIZE_MAX / sizeof(*call->pointer_arguments))
		return false;

	call->args = calloc(slots, sizeof(*call->args));
	call->arg_types = calloc(slots, sizeof(*call->arg_types));
	call->data = calloc(slots, sizeof(*call->data));
	call->pointer_arguments = calloc(slots, sizeof(*call->pointer_arguments));
	if (call->args == NULL || call->arg_types == NULL || call->data == NULL ||
		call->pointer_arguments == NULL) {
		po_ffi_call_release(call);
		return false;
	}
	call->arg_count = arg_count;
	return true;
}

static bool po_ffi_contract_scalar(const Po_FFI* binding, const Po_FFI_Call* call,
	size_t parameter_index, size_t* out_value)
{
	if (out_value == NULL)
		return false;
	if (parameter_index == POCO_BINDING_PARAMETER_NONE) {
		*out_value = 1;
		return true;
	}
	if (!po_ffi_contract_index_is_scalar(binding, parameter_index))
		return false;
	if (binding->arg_ido_types[parameter_index] == IDO_INT) {
		if (call->data[parameter_index].i < 0)
			return false;
		*out_value = (size_t)call->data[parameter_index].i;
	} else {
		if (call->data[parameter_index].l < 0)
			return false;
		*out_value = (size_t)call->data[parameter_index].l;
	}
	return true;
}

static bool po_ffi_popot_capacity(const Popot* pointer, size_t* out_capacity)
{
	uintptr_t current;
	uintptr_t minimum;
	uintptr_t maximum;

	if (pointer == NULL || out_capacity == NULL || pointer->pt == NULL ||
		pointer->min == NULL || pointer->max == NULL) {
		return false;
	}
	minimum = (uintptr_t)pointer->min;
	current = (uintptr_t)pointer->pt;
	maximum = (uintptr_t)pointer->max;
	if (minimum > current || current > maximum || maximum - current == UINTPTR_MAX)
		return false;
	*out_capacity = (size_t)(maximum - current + 1);
	return true;
}

static bool po_ffi_cstring_length(PocoPointerRegistry* pointer_registry,
	const Popot* pointer, size_t* out_length)
{
	size_t capacity;
	const char* terminator;

	if (!po_ffi_popot_capacity(pointer, &capacity) ||
		!poco_pointer_registry_validate(pointer_registry, pointer, capacity,
			POCO_POINTER_PERMISSION_READ)) {
		return false;
	}
	terminator = memchr(pointer->pt, '\0', capacity);
	if (terminator == NULL)
		return false;
	*out_length = (size_t)(terminator - (const char*)pointer->pt) + 1;
	return true;
}

static bool po_ffi_contract_span_length(const Po_FFI* binding,
	const Po_FFI_Call* call, PocoPointerRegistry* pointer_registry,
	PocoBindingSpanKind span_kind, size_t byte_count,
	size_t byte_count_parameter, size_t element_count_parameter,
	size_t string_parameter, size_t fallback_pointer_parameter,
	size_t* out_length)
{
	size_t first_factor;
	size_t second_factor;
	size_t source_length;
	size_t destination_length;
	size_t source_parameter;

	if (out_length == NULL)
		return false;
	switch (span_kind) {
		case POCO_BINDING_SPAN_BYTES:
			if (!po_ffi_contract_scalar(binding, call, byte_count_parameter, &first_factor) ||
				!po_ffi_contract_scalar(binding, call, element_count_parameter, &second_factor) ||
				byte_count != 0 && first_factor > SIZE_MAX / byte_count) {
				return false;
			}
			first_factor *= byte_count;
			if (second_factor != 0 && first_factor > SIZE_MAX / second_factor)
				return false;
			*out_length = first_factor * second_factor;
			return true;
		case POCO_BINDING_SPAN_C_STRING:
			source_parameter = string_parameter == POCO_BINDING_PARAMETER_NONE
				? fallback_pointer_parameter : string_parameter;
			if (!po_ffi_contract_index_is_pointer(binding, source_parameter))
				return false;
			return po_ffi_cstring_length(pointer_registry,
				&call->pointer_arguments[source_parameter], out_length);
		case POCO_BINDING_SPAN_APPEND_C_STRING:
			if (!po_ffi_contract_index_is_pointer(binding, fallback_pointer_parameter) ||
				!po_ffi_contract_index_is_pointer(binding, string_parameter) ||
				!po_ffi_cstring_length(pointer_registry,
					&call->pointer_arguments[fallback_pointer_parameter], &destination_length) ||
				!po_ffi_cstring_length(pointer_registry,
					&call->pointer_arguments[string_parameter], &source_length) ||
				destination_length > SIZE_MAX - source_length + 1) {
				return false;
			}
			*out_length = destination_length + source_length - 1;
			return true;
		default:
			return false;
	}
}

static bool po_ffi_validate_contract(const Po_FFI* binding, const Po_FFI_Call* call,
	PocoPointerRegistry* pointer_registry)
{
	const PocoBindingContract* contract = binding->contract;
	size_t index;

	if (contract == NULL)
		return true;
	for (index = 0; index < contract->pointer_contract_count; ++index) {
		const PocoBindingPointerContract* pointer = &contract->pointer_contracts[index];
		size_t byte_count;

		if (!po_ffi_contract_span_length(binding, call, pointer_registry,
			pointer->span_kind, pointer->byte_count, pointer->byte_count_parameter,
			pointer->element_count_parameter, pointer->string_parameter,
			pointer->parameter_index, &byte_count) ||
			!poco_pointer_registry_validate(pointer_registry,
				&call->pointer_arguments[pointer->parameter_index], byte_count,
				pointer->permissions)) {
			return false;
		}
	}
	return true;
}

static bool po_ffi_apply_pointer_return(const Po_FFI* binding,
	const Po_FFI_Call* call, PocoPointerRegistry* pointer_registry,
	void* raw_pointer, Popot* out_pointer)
{
	const PocoBindingReturnContract* contract;
	uintptr_t raw;

	if (out_pointer == NULL)
		return false;
	Popot_make_null(out_pointer);
	if (raw_pointer == NULL)
		return true;
	contract = binding->contract != NULL ? &binding->contract->return_value : NULL;
	if (contract == NULL || contract->origin == POCO_POINTER_RETURN_TRUSTED) {
		out_pointer->pt = raw_pointer;
		out_pointer->min = NULL;
		out_pointer->max = (void*)UINTPTR_MAX;
		return true;
	}
	if (contract->origin == POCO_POINTER_RETURN_ALIAS) {
		const Popot* alias = &call->pointer_arguments[contract->alias_parameter];
		uintptr_t minimum;
		uintptr_t maximum;

		if (alias->min == NULL || alias->max == NULL)
			return false;
		raw = (uintptr_t)raw_pointer;
		minimum = (uintptr_t)alias->min;
		maximum = (uintptr_t)alias->max;
		if (raw < minimum || raw > maximum)
			return false;
		*out_pointer = *alias;
		out_pointer->pt = raw_pointer;
		return true;
	}
	if (contract->origin == POCO_POINTER_RETURN_BORROWED) {
		uint32_t permissions;

		if (!poco_pointer_registry_find(pointer_registry, raw_pointer, out_pointer,
			&permissions) ||
			(permissions & contract->permissions) != contract->permissions) {
			return false;
		}
		return true;
	}
	if (contract->origin == POCO_POINTER_RETURN_OWNED) {
		size_t byte_count;
		uintptr_t start;
		uintptr_t end;

		if (!po_ffi_contract_span_length(binding, call, pointer_registry,
			contract->span_kind, contract->byte_count,
			contract->byte_count_parameter, contract->element_count_parameter,
			contract->string_parameter, POCO_BINDING_PARAMETER_NONE, &byte_count) ||
			!poco_pointer_span_range(raw_pointer, byte_count, &start, &end)) {
			return false;
		}
		if (pointer_registry != NULL && poco_pointer_registry_register_owned(pointer_registry,
			raw_pointer, byte_count, contract->permissions, contract->release,
			contract->release_user_data) != Success) {
			if (contract->release != NULL)
				contract->release(raw_pointer, contract->release_user_data);
			else
				free(raw_pointer);
			return false;
		}
		out_pointer->pt = raw_pointer;
		out_pointer->min = raw_pointer;
		out_pointer->max = (void*)end;
		return true;
	}
	return false;
}

/*
	Call the C function specified by the binding, grabbing passed parameters
	from the stack.  Call storage is allocated only after fixed and variadic
	argument counts have been combined and validated.

	Returns a Pt_num for acc.ret in runops.c.
*/
Pt_num po_ffi_call(const Po_FFI* binding, const Pt_num* stack_in,
	const Po_FFI_Variadic_Descriptor* variadic,
	PocoPointerRegistry* pointer_registry)
{
	Pt_num result;
	Po_FFI_Call call = {0};
	const Pt_num* stack = stack_in;
	unsigned int variadic_count = 0;
	unsigned int total_count;
	bool is_variadic;
	ffi_status status;
	unsigned int index;

	Popot_make_null(&result.ppt);
	if (binding == NULL) {
		builtin_err = Err_poco_ffi_func_not_found;
		return result;
	}

	is_variadic = po_ffi_is_variadic(binding);
	if (is_variadic) {
		if (variadic == NULL || (variadic->count != 0 && variadic->types == NULL)) {
			builtin_err = Err_poco_ffi_invalid_binding;
			return result;
		}
		if (variadic->count > UINT_MAX - binding->arg_count) {
			builtin_err = Err_poco_ffi_variadic_overflow;
			return result;
		}
		variadic_count = variadic->count;
	}
	if (!is_variadic && binding->arg_count != 0 && stack == NULL) {
		builtin_err = Err_poco_ffi_invalid_binding;
		return result;
	}

	total_count = binding->arg_count + variadic_count;
	if (!po_ffi_call_allocate(&call, total_count)) {
		builtin_err = Err_no_memory;
		return result;
	}

	for (index = 0; index < binding->arg_count; ++index) {
		call.arg_types[index] = binding->arg_types[index];
		switch (binding->arg_ido_types[index]) {
			case IDO_INT:
				call.data[index].i = stack->i;
				call.args[index] = &call.data[index].i;
				stack = (const Pt_num*)OPTR(stack, sizeof(int));
				break;
			case IDO_LONG:
				call.data[index].l = stack->l;
				call.args[index] = &call.data[index].l;
				stack = (const Pt_num*)OPTR(stack, sizeof(long));
				break;
			case IDO_DOUBLE:
				call.data[index].d = stack->d;
				call.args[index] = &call.data[index].d;
				stack = (const Pt_num*)OPTR(stack, sizeof(double));
				break;
			case IDO_POINTER:
				call.pointer_arguments[index] = stack->ppt;
				call.data[index].p = stack->ppt.pt;
				call.args[index] = &call.data[index].p;
				stack = (const Pt_num*)OPTR(stack, sizeof(Popot));
				break;
			case IDO_CPT:
				call.data[index].p = stack->p;
				call.args[index] = &call.data[index].p;
				stack = (const Pt_num*)OPTR(stack, sizeof(void*));
				break;
			default:
				builtin_err = Err_poco_ffi_invalid_binding;
				goto CLEANUP;
		}
	}
	if (!po_ffi_validate_contract(binding, &call, pointer_registry)) {
		poco_set_error("FFI call '%s' exceeds its contracted pointer span.", binding->name);
		builtin_err = Err_poco_ffi_bounds;
		goto CLEANUP;
	}

	for (index = 0; index < variadic_count; ++index) {
		const ffi_type* type = variadic->types[index];
		const size_t size = type == NULL ? 0 : po_ffi_argtype_size(type);
		const unsigned int call_index = binding->arg_count + index;

		if (size == 0 || size > sizeof(call.data[call_index])) {
			builtin_err = Err_poco_ffi_invalid_binding;
			goto CLEANUP;
		}
		memcpy(&call.data[call_index], stack, size);
		call.args[call_index] = &call.data[call_index];
		call.arg_types[call_index] = (ffi_type*)type;
		stack = (const Pt_num*)OPTR(stack, size);
	}

	call.args[call.arg_count] = NULL;
	call.arg_types[call.arg_count] = NULL;
	if (is_variadic) {
		status = ffi_prep_cif_var(&call.interface, FFI_DEFAULT_ABI,
			binding->arg_count, call.arg_count, binding->result_type, call.arg_types);
	} else {
		status = ffi_prep_cif(&call.interface, FFI_DEFAULT_ABI,
			call.arg_count, binding->result_type, call.arg_types);
	}
	if (status != FFI_OK) {
		poco_set_error("FFI call '%s' could not prepare its call interface: %s.",
			binding->name, po_ffi_status_str(status));
		builtin_err = Err_poco_ffi_invalid_binding;
		goto CLEANUP;
	}

	ffi_call(&call.interface, FFI_FN(binding->function),
		binding->result_ido_type == IDO_VOID ? NULL : &call.result, call.args);
	switch (binding->result_ido_type) {
		case IDO_INT:
			result.i = call.result.i;
			break;
		case IDO_LONG:
			result.l = call.result.l;
			break;
		case IDO_DOUBLE:
			result.d = call.result.d;
			break;
		case IDO_POINTER:
			if (!po_ffi_apply_pointer_return(binding, &call, pointer_registry,
				call.result.p, &result.ppt)) {
				poco_set_error("FFI call '%s' returned a pointer outside its contract.",
					binding->name);
				builtin_err = Err_poco_ffi_bounds;
			}
			break;
		case IDO_CPT:
			result.p = call.result.p;
			break;
		default:
			break;
	}

CLEANUP:
	po_ffi_call_release(&call);
	return result;
}
