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
#include "activation.h"
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

struct po_ffi_owned_type {
	struct po_ffi_owned_type* next;
	ffi_type type;
	ffi_type** elements;
};

typedef struct poco_pointer_span {
	struct poco_pointer_span* next;
	void* pointer;
	size_t byte_count;
	uint32_t permissions;
	PocoOwnedPointerRelease release;
	void* release_user_data;
	bool active;
	bool owned;
} PocoPointerSpan;

struct poco_pointer_registry {
	PocoPointerSpan* spans;
	PocoPointerRegistry* parent;
};

size_t po_ffi_funcmap_hash(const void* data);
int po_ffi_func_compare(const void* left, const void* right);
static const char* po_ffi_status_str(const ffi_status status);

/* Keep every runtime cif preparation observable by the activation-local test
 * counters.  Binding construction passes no activation because its one-time
 * preparation is intentionally outside execution. */
static ffi_status po_ffi_prepare_fixed_cif(PocoActivation* activation, ffi_cif* interface,
										   unsigned int argument_count, ffi_type* result_type,
										   ffi_type** argument_types)
{
	if (activation != NULL) {
		++activation->ffi_fixed_call_prep_count;
	}
	return ffi_prep_cif(interface, FFI_DEFAULT_ABI, argument_count, result_type, argument_types);
}

static ffi_status po_ffi_prepare_variadic_cif(PocoActivation* activation, ffi_cif* interface,
											  unsigned int fixed_count, unsigned int argument_count,
											  ffi_type* result_type, ffi_type** argument_types)
{
	if (activation != NULL) {
		++activation->ffi_variadic_call_prep_count;
	}
	return ffi_prep_cif_var(interface, FFI_DEFAULT_ABI, fixed_count, argument_count, result_type,
							argument_types);
}

static bool poco_pointer_span_range(const void* pointer, size_t byte_count, uintptr_t* out_start,
									uintptr_t* out_end)
{
	uintptr_t start;

	if (pointer == NULL || byte_count == 0 || out_start == NULL || out_end == NULL) {
		return false;
	}
	start = (uintptr_t)pointer;
	if (byte_count - 1 > UINTPTR_MAX - start) {
		return false;
	}
	*out_start = start;
	*out_end = start + byte_count - 1;
	return true;
}

static PocoPointerSpan* poco_pointer_registry_find_span(PocoPointerRegistry* registry,
														const void* pointer, size_t byte_count,
														bool* out_stale)
{
	PocoPointerRegistry* current_registry;
	PocoPointerSpan* span;
	PocoPointerSpan* stale = NULL;
	uintptr_t pointer_start;
	uintptr_t pointer_end;

	if (out_stale != NULL) {
		*out_stale = false;
	}
	if (registry == NULL ||
		!poco_pointer_span_range(pointer, byte_count, &pointer_start, &pointer_end)) {
		return NULL;
	}
	for (current_registry = registry; current_registry != NULL;
		 current_registry = current_registry->parent) {
		for (span = current_registry->spans; span != NULL; span = span->next) {
			uintptr_t span_start;
			uintptr_t span_end;

			if (!poco_pointer_span_range(span->pointer, span->byte_count, &span_start, &span_end) ||
				pointer_start < span_start || pointer_end > span_end) {
				continue;
			}
			if (span->active) {
				return span;
			}
			stale = span;
		}
	}
	if (out_stale != NULL && stale != NULL) {
		*out_stale = true;
	}
	return NULL;
}

PocoPointerRegistry* poco_pointer_registry_create(void)
{
	return calloc(1, sizeof(PocoPointerRegistry));
}

PocoPointerRegistry* poco_pointer_registry_create_child(PocoPointerRegistry* parent)
{
	PocoPointerRegistry* registry = poco_pointer_registry_create();

	if (registry == NULL) {
		return NULL;
	}
	while (parent != NULL && parent->parent != NULL) {
		parent = parent->parent;
	}
	registry->parent = parent;
	return registry;
}

void poco_pointer_registry_destroy(PocoPointerRegistry* registry)
{
	PocoPointerSpan* span;
	PocoPointerSpan* next;

	if (registry == NULL) {
		return;
	}
	for (span = registry->spans; span != NULL; span = next) {
		next = span->next;
		if (span->active && span->owned && span->pointer != NULL) {
			if (span->release != NULL) {
				span->release(span->pointer, span->release_user_data);
			} else {
				free(span->pointer);
			}
		}
		free(span);
	}
	free(registry);
}

static Errcode poco_pointer_registry_register(PocoPointerRegistry* registry, void* pointer,
											  size_t byte_count, uint32_t permissions, bool owned,
											  PocoOwnedPointerRelease release,
											  void* release_user_data)
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
	/* Activation-local borrowed registrations are call-scoped overlays. Keep
	 * duplicate addresses as a stack so a nested call can temporarily narrow or
	 * widen the same host span, then reveal its enclosing registration again. */
	if (registry->parent == NULL || owned) {
		for (span = registry->spans; span != NULL; span = span->next) {
			if (span->pointer == pointer) {
				if (span->active && span->owned != owned) {
					return Err_parameter_range;
				}
				span->byte_count = byte_count;
				span->permissions = permissions;
				span->owned = owned;
				span->release = release;
				span->release_user_data = release_user_data;
				span->active = true;
				return Success;
			}
		}
	}
	span = calloc(1, sizeof(*span));
	if (span == NULL) {
		return Err_no_memory;
	}
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

Errcode poco_pointer_registry_register_borrowed(PocoPointerRegistry* registry, void* pointer,
												size_t byte_count, uint32_t permissions)
{
	return poco_pointer_registry_register(registry, pointer, byte_count, permissions, false, NULL,
										  NULL);
}

Errcode poco_pointer_registry_unregister_borrowed(PocoPointerRegistry* registry,
												  const void* pointer)
{
	PocoPointerSpan* span;

	if (registry == NULL || pointer == NULL) {
		return Err_parameter_range;
	}
	for (span = registry->spans; span != NULL; span = span->next) {
		if (span->pointer == pointer && !span->owned) {
			if (!span->active) {
				continue;
			}
			span->active = false;
			return Success;
		}
	}
	return Err_not_found;
}

Errcode poco_pointer_registry_register_owned(PocoPointerRegistry* registry, void* pointer,
											 size_t byte_count, uint32_t permissions,
											 PocoOwnedPointerRelease release,
											 void* release_user_data)
{
	while (registry != NULL && registry->parent != NULL) {
		registry = registry->parent;
	}
	return poco_pointer_registry_register(registry, pointer, byte_count, permissions, true, release,
										  release_user_data);
}

Errcode poco_pointer_registry_release_owned(PocoPointerRegistry* registry, const void* pointer)
{
	PocoPointerSpan* span;

	while (registry != NULL && registry->parent != NULL) {
		registry = registry->parent;
	}

	if (registry == NULL || pointer == NULL) {
		return Err_parameter_range;
	}
	for (span = registry->spans; span != NULL; span = span->next) {
		if (span->pointer == pointer && span->owned) {
			if (!span->active) {
				return Err_not_found;
			}
			span->active = false;
			return Success;
		}
	}
	return Err_not_found;
}

bool poco_pointer_registry_validate(PocoPointerRegistry* registry, const Popot* pointer,
									size_t byte_count, uint32_t permissions)
{
	PocoPointerSpan* span;
	uintptr_t minimum;
	uintptr_t current;
	uintptr_t maximum;
	uintptr_t required_end;
	bool stale;

	if (byte_count == 0) {
		return true;
	}
	if (pointer == NULL || pointer->pt == NULL || pointer->min == NULL || pointer->max == NULL) {
		return false;
	}
	minimum = (uintptr_t)pointer->min;
	current = (uintptr_t)pointer->pt;
	maximum = (uintptr_t)pointer->max;
	if (minimum > current || current > maximum || byte_count - 1 > UINTPTR_MAX - current) {
		return false;
	}
	required_end = current + byte_count - 1;
	if (required_end > maximum) {
		return false;
	}
	span = poco_pointer_registry_find_span(registry, pointer->pt, byte_count, &stale);
	if (span == NULL) {
		return !stale;
	}
	return (span->permissions & permissions) == permissions;
}

bool poco_pointer_registry_is_managed(PocoPointerRegistry* registry, const Popot* pointer)
{
	bool stale;

	if (registry == NULL || pointer == NULL || pointer->min == NULL) {
		return false;
	}
	return poco_pointer_registry_find_span(registry, pointer->min, 1, &stale) != NULL || stale;
}

bool poco_pointer_registry_find(PocoPointerRegistry* registry, const void* pointer,
								Popot* out_pointer, uint32_t* out_permissions)
{
	PocoPointerSpan* span;
	uintptr_t start;
	uintptr_t end;
	bool stale;

	if (out_pointer == NULL || !poco_pointer_span_range(pointer, 1, &start, &end)) {
		return false;
	}
	span = poco_pointer_registry_find_span(registry, pointer, 1, &stale);
	if (span == NULL) {
		return false;
	}
	if (!poco_pointer_span_range(span->pointer, span->byte_count, &start, &end)) {
		return false;
	}
	out_pointer->pt = (void*)pointer;
	out_pointer->min = span->pointer;
	out_pointer->max = (void*)end;
	if (out_permissions != NULL) {
		*out_permissions = span->permissions;
	}
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
		case IDO_STRUCT:
			/* Aggregate descriptors require the parameter's Struct_info. */
			return NULL;

		case IDO_VOID:
			return &ffi_type_void;

		default:
			return NULL;
	}
}

/* Poco deliberately computes both source float and source double values in the
 * IDO_DOUBLE class.  Preserve that VM representation while retaining the
 * source-level distinction needed at a native ABI boundary. */
static bool po_ffi_type_info_is_scalar_float(const Type_info* type_info)
{
	return type_info != NULL && type_info->ido_type == IDO_DOUBLE && type_info->comp_count == 1 &&
		   type_info->comp != NULL && type_info->comp[0] == TYPE_FLOAT;
}

/* The expression capability tier excludes every script-visible native pointer,
 * including pointers nested inside by-value aggregates.  Array storage inside
 * a struct remains an aggregate; only TYPE_POINTER/TYPE_CPT components carry
 * native pointer access. */
static bool po_ffi_type_info_contains_pointer(const Type_info* type_info)
{
	const Struct_info* struct_info;
	const Symbol* member;
	int index;

	if (type_info == NULL || type_info->comp == NULL) {
		return false;
	}
	for (index = 0; index < type_info->comp_count; ++index) {
		if (type_info->comp[index] == TYPE_POINTER || type_info->comp[index] == TYPE_CPT) {
			return true;
		}
	}
	if (type_info->comp_count == 0 || type_info->comp[0] != TYPE_STRUCT ||
		type_info->sdims == NULL) {
		return false;
	}
	struct_info = (const Struct_info*)type_info->sdims[0].pt;
	if (struct_info == NULL) {
		return false;
	}
	for (member = struct_info->elements; member != NULL; member = member->next) {
		if (po_ffi_type_info_contains_pointer(member->ti)) {
			return true;
		}
	}
	return false;
}

/* A by-value struct parameter or return carries exactly one TYPE_STRUCT
 * component whose single struct dimension resolves to layout metadata. */
static bool po_ffi_struct_type_info_is_valid(const Type_info* type_info)
{
	return type_info != NULL && type_info->comp_count == 1 && type_info->comp != NULL &&
		   type_info->comp[0] == TYPE_STRUCT && type_info->sdims != NULL &&
		   type_info->sdims[0].pt != NULL;
}

static ffi_type* po_ffi_scalar_call_type(const Type_info* type_info)
{
	if (po_ffi_type_info_is_scalar_float(type_info)) {
		return &ffi_type_float;
	}
	return type_info == NULL ? NULL : po_ffi_type_from_ido_type(type_info->ido_type);
}

static void po_ffi_owned_types_delete(Po_FFI_Owned_Type* owned)
{
	Po_FFI_Owned_Type* next;

	while (owned != NULL) {
		next = owned->next;
		free(owned->elements);
		free(owned);
		owned = next;
	}
}

static ffi_type* po_ffi_scalar_type_from_type_comp(TypeComp type)
{
	switch (type) {
		case TYPE_CHAR:
			return &ffi_type_schar;
		case TYPE_UCHAR:
			return &ffi_type_uchar;
		case TYPE_SHORT:
			return &ffi_type_sshort;
		case TYPE_USHORT:
			return &ffi_type_ushort;
		case TYPE_INT:
			return &ffi_type_sint;
		case TYPE_UINT:
			return &ffi_type_uint;
		case TYPE_LONG:
			return &ffi_type_slong;
		case TYPE_ULONG:
			return &ffi_type_ulong;
		case TYPE_FLOAT:
			return &ffi_type_float;
		case TYPE_DOUBLE:
			return &ffi_type_double;
		case TYPE_POINTER:
		case TYPE_CPT:
			return &ffi_type_pointer;
		default:
			return NULL;
	}
}

static Errcode po_ffi_owned_aggregate_new(size_t element_count, Po_FFI_Owned_Type** owned_types,
										  Po_FFI_Owned_Type** out_owned)
{
	Po_FFI_Owned_Type* owned;

	if (owned_types == NULL || out_owned == NULL ||
		element_count > (SIZE_MAX / sizeof(*owned->elements)) - 1) {
		return Err_poco_ffi_invalid_binding;
	}
	*out_owned = NULL;
	owned = calloc(1, sizeof(*owned));
	if (owned == NULL) {
		return Err_no_memory;
	}
	owned->elements = calloc(element_count + 1, sizeof(*owned->elements));
	if (owned->elements == NULL) {
		free(owned);
		return Err_no_memory;
	}
	owned->type.type = FFI_TYPE_STRUCT;
	owned->type.elements = owned->elements;
	owned->next = *owned_types;
	*owned_types = owned;
	*out_owned = owned;
	return Success;
}

static Errcode po_ffi_type_from_type_info(const Type_info* type_info, unsigned int component_count,
										  Po_FFI_Owned_Type** owned_types, ffi_type** out_type);

static Errcode po_ffi_type_from_struct_info_impl(const Struct_info* struct_info,
												 Po_FFI_Owned_Type** owned_types,
												 ffi_type** out_type)
{
	Po_FFI_Owned_Type* owned;
	const Symbol* member;
	size_t index;
	Errcode err;

	if (out_type == NULL) {
		return Err_poco_ffi_invalid_binding;
	}
	*out_type = NULL;
	if (struct_info == NULL || struct_info->type != TYPE_STRUCT || struct_info->el_count <= 0) {
		return Err_poco_ffi_invalid_binding;
	}
	err = po_ffi_owned_aggregate_new((size_t)struct_info->el_count, owned_types, &owned);
	if (err < Success) {
		return err;
	}
	member = struct_info->elements;
	for (index = 0; index < (size_t)struct_info->el_count; ++index) {
		if (member == NULL || member->ti == NULL) {
			return Err_poco_ffi_invalid_binding;
		}
		err = po_ffi_type_from_type_info(member->ti, member->ti->comp_count, owned_types,
										 &owned->elements[index]);
		if (err < Success) {
			return err;
		}
		member = member->next;
	}
	if (member != NULL) {
		return Err_poco_ffi_invalid_binding;
	}
	*out_type = &owned->type;
	return Success;
}

static Errcode po_ffi_type_from_type_info(const Type_info* type_info, unsigned int component_count,
										  Po_FFI_Owned_Type** owned_types, ffi_type** out_type)
{
	TypeComp component;
	Po_FFI_Owned_Type* owned;
	ffi_type* element_type;
	long array_count;
	long index;
	Errcode err;

	if (out_type == NULL) {
		return Err_poco_ffi_invalid_binding;
	}
	*out_type = NULL;
	if (type_info == NULL || component_count == 0 || component_count > type_info->comp_count ||
		type_info->comp == NULL || type_info->sdims == NULL) {
		return Err_poco_ffi_invalid_binding;
	}
	component = type_info->comp[component_count - 1];
	if (component == TYPE_POINTER || component == TYPE_CPT) {
		*out_type = &ffi_type_pointer;
		return Success;
	}
	if (component == TYPE_ARRAY) {
		array_count = type_info->sdims[component_count - 1].l;
		if (array_count <= 0) {
			return Err_poco_ffi_invalid_binding;
		}
		err =
			po_ffi_type_from_type_info(type_info, component_count - 1, owned_types, &element_type);
		if (err < Success) {
			return err;
		}
		err = po_ffi_owned_aggregate_new((size_t)array_count, owned_types, &owned);
		if (err < Success) {
			return err;
		}
		for (index = 0; index < array_count; ++index) {
			owned->elements[index] = element_type;
		}
		*out_type = &owned->type;
		return Success;
	}
	if (component_count != 1) {
		return Err_poco_ffi_invalid_binding;
	}
	if (component == TYPE_STRUCT) {
		return po_ffi_type_from_struct_info_impl((const Struct_info*)type_info->sdims[0].pt,
												 owned_types, out_type);
	}
	*out_type = po_ffi_scalar_type_from_type_comp(component);
	return *out_type == NULL ? Err_poco_ffi_invalid_binding : Success;
}

Errcode po_ffi_type_from_struct_info(Po_FFI* binding, const Struct_info* struct_info,
									 ffi_type** out_type)
{
	Po_FFI_Owned_Type* owned_types = NULL;
	Po_FFI_Owned_Type* tail;
	ffi_type* type = NULL;
	Errcode err;

	if (out_type == NULL) {
		return Err_poco_ffi_invalid_binding;
	}
	*out_type = NULL;
	if (binding == NULL) {
		return Err_poco_ffi_invalid_binding;
	}
	err = po_ffi_type_from_struct_info_impl(struct_info, &owned_types, &type);
	if (err < Success) {
		po_ffi_owned_types_delete(owned_types);
		return err;
	}
	tail = owned_types;
	while (tail->next != NULL) {
		tail = tail->next;
	}
	tail->next = binding->owned_types;
	binding->owned_types = owned_types;
	*out_type = type;
	return Success;
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
		case IDO_STRUCT:
			return sizeof(Popot);

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
		   (binding->arg_ido_types[index] == IDO_INT || binding->arg_ido_types[index] == IDO_LONG);
}

static bool po_ffi_contract_index_is_pointer(const Po_FFI* binding, size_t index)
{
	return index < binding->arg_count && binding->arg_ido_types[index] == IDO_POINTER;
}

static uint32_t po_ffi_pointer_depth(const Type_info* type)
{
	uint32_t depth = 0;
	int index;

	if (type == NULL) {
		return 0;
	}
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
	return index == POCO_BINDING_PARAMETER_NONE || po_ffi_contract_index_is_scalar(binding, index);
}

static bool po_ffi_contract_is_valid(const Po_FFI* binding)
{
	const PocoBindingContract* contract = binding->contract;
	size_t index;

	if (contract == NULL) {
		return true;
	}
	if (contract->pointer_contract_count != 0 && contract->pointer_contracts == NULL) {
		return false;
	}
	for (index = 0; index < contract->pointer_contract_count; ++index) {
		const PocoBindingPointerContract* pointer = &contract->pointer_contracts[index];

		if (!po_ffi_contract_index_is_pointer(binding, pointer->parameter_index) ||
			pointer->pointer_depth == 0 ||
			binding->arg_pointer_depths[pointer->parameter_index] != pointer->pointer_depth ||
			(pointer->permissions &
			 (POCO_POINTER_PERMISSION_READ | POCO_POINTER_PERMISSION_WRITE)) == 0 ||
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
			!po_ffi_contract_index_is_pointer(binding, contract->return_value.alias_parameter)) {
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
static Errcode po_ffi_create_binding(PocoVm* vm, const C_frame* frame, Po_FFI** out_binding)
{
	Po_FFI* binding;
	Symbol* param;
	unsigned int fixed_count = 0;
	unsigned int interface_count;
	unsigned int index;
	bool is_variadic = false;
	ffi_type* type;
	ffi_status ffi_status;
	Errcode err;

	if (out_binding == NULL) {
		return Err_poco_ffi_invalid_binding;
	}
	*out_binding = NULL;
	if (frame == NULL || frame->name == NULL || frame->name[0] == '\0') {
		poco_set_error(vm, "FFI binding has no name.");
		return Err_poco_ffi_invalid_binding;
	}
	if (frame->code_pt == NULL) {
		poco_set_error(vm, "FFI binding '%s' has a null function.", frame->name);
		return Err_poco_ffi_invalid_binding;
	}
	if (frame->pcount < 0) {
		poco_set_error(vm, "FFI binding '%s' has an invalid parameter count.", frame->name);
		return Err_poco_ffi_invalid_binding;
	}
	if (vm != NULL && vm->untrusted_expression_library_registered &&
		po_ffi_type_info_contains_pointer(frame->return_type)) {
		poco_set_error(vm,
					   "Untrusted expression tier excludes pointer result from FFI binding '%s'.",
					   frame->name);
		return Err_poco_ffi_invalid_binding;
	}

	param = frame->parameters;
	for (index = 0; index < (unsigned int)frame->pcount; ++index) {
		if (param == NULL || param->ti == NULL) {
			poco_set_error(vm, "FFI binding '%s' has incomplete parameter metadata.", frame->name);
			return Err_poco_ffi_invalid_binding;
		}
		if (param->ti->comp_count != 0 && param->ti->comp == NULL) {
			poco_set_error(vm, "FFI binding '%s' has incomplete parameter type metadata.",
						   frame->name);
			return Err_poco_ffi_invalid_binding;
		}
		if (param->ti->comp_count != 0 && param->ti->comp[0] == TYPE_ELLIPSIS) {
			if (vm != NULL && vm->untrusted_expression_library_registered) {
				poco_set_error(vm, "Untrusted expression tier excludes variadic FFI binding '%s'.",
							   frame->name);
				return Err_poco_ffi_invalid_binding;
			}
			if (is_variadic || index + 1 != (unsigned int)frame->pcount) {
				poco_set_error(vm, "FFI binding '%s' has an invalid variadic declaration.",
							   frame->name);
				return Err_poco_ffi_invalid_binding;
			}
			is_variadic = true;
		} else {
			if (vm != NULL && vm->untrusted_expression_library_registered &&
				po_ffi_type_info_contains_pointer(param->ti)) {
				poco_set_error(
					vm,
					"Untrusted expression tier excludes pointer parameter from FFI binding '%s'.",
					frame->name);
				return Err_poco_ffi_invalid_binding;
			}
			if (param->ti->ido_type == IDO_STRUCT) {
				if (!po_ffi_struct_type_info_is_valid(param->ti)) {
					poco_set_error(vm, "FFI binding '%s' has incomplete struct parameter metadata.",
								   frame->name);
					return Err_poco_ffi_invalid_binding;
				}
			} else {
				type = po_ffi_scalar_call_type(param->ti);
			}
			if (param->ti->ido_type != IDO_STRUCT && (type == NULL || type == &ffi_type_void)) {
				poco_set_error(vm, "FFI binding '%s' has an unsupported parameter type.",
							   frame->name);
				return Err_poco_ffi_invalid_binding;
			}
			++fixed_count;
		}
		param = param->link;
	}
	if (param != NULL) {
		poco_set_error(vm, "FFI binding '%s' has inconsistent parameter metadata.", frame->name);
		return Err_poco_ffi_invalid_binding;
	}
	if (is_variadic && fixed_count == 0) {
		poco_set_error(vm, "FFI binding '%s' has no fixed parameter before '...'.", frame->name);
		return Err_poco_ffi_invalid_binding;
	}

	if (frame->return_type != NULL && frame->return_type->ido_type == IDO_STRUCT) {
		if (!po_ffi_struct_type_info_is_valid(frame->return_type)) {
			poco_set_error(vm, "FFI binding '%s' has incomplete struct return metadata.",
						   frame->name);
			return Err_poco_ffi_invalid_binding;
		}
		type = NULL;
	} else {
		type = frame->return_type == NULL ? &ffi_type_void
										  : po_ffi_scalar_call_type(frame->return_type);
	}
	if (frame->return_type == NULL || frame->return_type->ido_type != IDO_STRUCT) {
		if (type == NULL) {
			poco_set_error(vm, "FFI binding '%s' has an unsupported return type.", frame->name);
			return Err_poco_ffi_invalid_binding;
		}
	}

	binding = calloc(1, sizeof(*binding));
	if (binding == NULL) {
		poco_set_error(vm, "Unable to allocate FFI binding '%s'.", frame->name);
		return Err_no_memory;
	}
	binding->name = frame->name;
	binding->function = frame->code_pt;
	binding->contract = frame->binding_contract;
	if ((frame->binding_flags & POCO_BINDING_RUN_CONTEXT) != 0) {
		binding->flags |= PO_FFI_RUN_CONTEXT;
	}
	binding->arg_count = fixed_count;
	binding->result_ido_type = frame->return_type == NULL ? IDO_VOID : frame->return_type->ido_type;
	if (binding->result_ido_type == IDO_STRUCT) {
		err = po_ffi_type_from_struct_info(
			binding, (const Struct_info*)frame->return_type->sdims[0].pt, &binding->result_type);
		if (err < Success) {
			poco_set_error(vm, "FFI binding '%s' has an invalid struct return type.",
						   binding->name);
			po_ffi_delete(binding);
			return err;
		}
	} else {
		binding->result_type = type;
	}
	/* The descriptor is complete before any argument metadata is published. */
	if (binding->result_type == NULL) {
		poco_set_error(vm, "FFI binding '%s' has an unsupported return type.", frame->name);
		po_ffi_delete(binding);
		return Err_poco_ffi_invalid_binding;
	}
	/* One run-context argument plus a checked trailing slot can follow the
	 * declared fixed arguments. */
	binding->arg_types = calloc((size_t)fixed_count + 2, sizeof(*binding->arg_types));
	binding->arg_ido_types = calloc((size_t)fixed_count + 2, sizeof(*binding->arg_ido_types));
	binding->arg_pointer_depths =
		calloc((size_t)fixed_count + 2, sizeof(*binding->arg_pointer_depths));
	if (binding->arg_types == NULL || binding->arg_ido_types == NULL ||
		binding->arg_pointer_depths == NULL) {
		poco_set_error(vm, "Unable to allocate FFI argument metadata for '%s'.", frame->name);
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
			if (param->ti->ido_type == IDO_STRUCT) {
				err = po_ffi_type_from_struct_info(binding,
												   (const Struct_info*)param->ti->sdims[0].pt,
												   &binding->arg_types[index]);
				if (err < Success) {
					poco_set_error(vm, "FFI binding '%s' has an invalid struct parameter.",
								   binding->name);
					po_ffi_delete(binding);
					return err;
				}
			} else {
				binding->arg_types[index] = po_ffi_scalar_call_type(param->ti);
			}
			binding->arg_pointer_depths[index] = po_ffi_pointer_depth(param->ti);
			++index;
		}
		param = param->link;
	}
	interface_count = binding->arg_count;
	if ((binding->flags & PO_FFI_RUN_CONTEXT) != 0) {
		binding->arg_types[interface_count++] = &ffi_type_pointer;
	}

	if (is_variadic) {
		ffi_status =
			po_ffi_prepare_variadic_cif(NULL, &binding->interface, interface_count, interface_count,
										binding->result_type, binding->arg_types);
	} else {
		ffi_status = po_ffi_prepare_fixed_cif(NULL, &binding->interface, interface_count,
											  binding->result_type, binding->arg_types);
	}
	if (ffi_status != FFI_OK) {
		poco_set_error(vm, "FFI binding '%s' could not prepare its call interface: %s.",
					   binding->name, po_ffi_status_str(ffi_status));
		po_ffi_delete(binding);
		return Err_poco_ffi_invalid_binding;
	}
	if (!po_ffi_contract_is_valid(binding)) {
		poco_set_error(vm, "FFI binding '%s' has an invalid pointer contract (result %d, args %u).",
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

	(void)po_ffi_create_binding(NULL, frame, &binding);
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
	if (type == &ffi_type_float) {
		return true;
	}
	if (sizeof(int) > sizeof(int8_t) && (type == &ffi_type_uint8 || type == &ffi_type_sint8)) {
		return true;
	}
	if (sizeof(int) > sizeof(int16_t) && (type == &ffi_type_uint16 || type == &ffi_type_sint16)) {
		return true;
	}
	return false;
}

void po_ffi_variadic_types_reset(Po_FFI_Variadic_Descriptor* variadic)
{
	if (variadic != NULL) {
		variadic->count = 0;
	}
}

void po_ffi_variadic_types_release(Po_FFI_Variadic_Descriptor* variadic)
{
	if (variadic == NULL) {
		return;
	}
	free(variadic->types);
	memset(variadic, 0, sizeof(*variadic));
}

Errcode po_ffi_variadic_types_append(PocoVm* vm, Po_FFI_Variadic_Descriptor* variadic,
									 ffi_type* type)
{
	unsigned int new_capacity;
	ffi_type** new_types;

	if (variadic == NULL || type == NULL || type == &ffi_type_void ||
		po_ffi_variadic_type_needs_promotion(type) || po_ffi_argtype_size(type) == 0) {
		poco_set_error(vm, "Unsupported variadic FFI argument type '%s'.",
					   po_ffi_name_for_type(type));
		return Err_poco_ffi_invalid_binding;
	}
	if (variadic->count == UINT_MAX) {
		return Err_poco_ffi_variadic_overflow;
	}
	if (variadic->count == variadic->capacity) {
		if (variadic->capacity == 0) {
			new_capacity = 8;
		} else if (variadic->capacity > UINT_MAX / 2) {
			return Err_poco_ffi_variadic_overflow;
		} else {
			new_capacity = variadic->capacity * 2;
		}
		if ((size_t)new_capacity > SIZE_MAX / sizeof(*variadic->types)) {
			return Err_poco_ffi_variadic_overflow;
		}
		new_types = realloc(variadic->types, (size_t)new_capacity * sizeof(*variadic->types));
		if (new_types == NULL) {
			return Err_no_memory;
		}
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
	if (binding == NULL) {
		return;
	}
	po_ffi_owned_types_delete(binding->owned_types);
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

	if (func_map == NULL) {
		return;
	}

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
	if (env == NULL) {
		return;
	}
	po_ffi_variadic_types_release(&env->variadic);
	if (env->func_map == NULL) {
		return;
	}
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
Po_FFI* po_ffi_find_binding(PocoActivation* env, const void* key)
{
	if (!env->code->ffi_bindings) {
		fprintf(stderr, "%s: called on env with NULL map.\n", __FUNCTION__);
		return NULL;
	}

	Po_FFI* binding = hashmap_get(&((Po_FuncMap*)env->code->ffi_bindings)->map, key);
	if (!binding) {
		env->builtin_error = Err_poco_ffi_func_not_found;
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
		poco_set_error(env != NULL ? env->vm : NULL,
					   "FFI structures have already been built for this program.");
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

		err = po_ffi_create_binding(env->vm, frame, &binding);
		if (err != Success) {
			goto ERROR;
		}
		{
			void* existing_function = hashmap_get(&env->func_map->name_map, binding->name);
			Po_FFI* existing_binding = hashmap_get(&env->func_map->map, binding->function);

			/* Every translation unit parses the registered prototype catalog in
			 * its own root scope. Collapse those identical native frames when the
			 * shared program builds its one runtime dispatch map. */
			if (existing_binding != NULL && existing_function == binding->function) {
				po_ffi_delete(binding);
				continue;
			}
		}
		if (hashmap_get(&env->func_map->name_map, binding->name) != NULL) {
			poco_set_error(env != NULL ? env->vm : NULL,
						   "FFI binding '%s' duplicates an existing function name.", binding->name);
			po_ffi_delete(binding);
			err = Err_poco_ffi_invalid_binding;
			goto ERROR;
		}
		if (hashmap_get(&env->func_map->map, binding->function) != NULL) {
			poco_set_error(env != NULL ? env->vm : NULL,
						   "FFI binding '%s' duplicates an existing function address.",
						   binding->name);
			po_ffi_delete(binding);
			err = Err_poco_ffi_invalid_binding;
			goto ERROR;
		}

		put_result = hashmap_put(&env->func_map->name_map, binding->name, binding->function);
		if (put_result < 0) {
			poco_set_error(env != NULL ? env->vm : NULL,
						   "Unable to add FFI binding '%s' to the name map.", binding->name);
			po_ffi_delete(binding);
			err = Err_poco_ffi_no_map_insert;
			goto ERROR;
		}
		put_result = hashmap_put(&env->func_map->map, binding->function, binding);
		if (put_result < 0) {
			hashmap_remove(&env->func_map->name_map, binding->name);
			poco_set_error(env != NULL ? env->vm : NULL,
						   "Unable to add FFI binding '%s' to the address map.", binding->name);
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
typedef struct po_ffi_call {
	ffi_cif interface;
	unsigned int arg_count;
	void** args;
	ffi_type** arg_types;
	Po_FFI_Data* data;
	Popot* pointer_arguments;
	void** struct_arguments;
	Po_FFI_Data result;
} Po_FFI_Call;

typedef struct po_ffi_activation_call {
	struct po_ffi_activation_call* next;
	const Po_FFI* binding;
	Po_FFI_Call call;
} Po_FFI_Activation_Call;

static bool po_ffi_struct_result_prepare(PocoActivation* activation, const ffi_type* type,
										 Popot* out_result)
{
	void* allocation;
	void* aligned;
	size_t allocation_size;
	size_t requested_capacity;
	size_t alignment;
	uintptr_t address;
	uintptr_t remainder;

	if (activation == NULL || type == NULL || out_result == NULL || type->size == 0) {
		return false;
	}
	alignment = type->alignment != 0 ? type->alignment : 1;
	if (activation->ffi_struct_result != NULL &&
		activation->ffi_struct_result_capacity >= type->size &&
		(uintptr_t)activation->ffi_struct_result % alignment == 0) {
		aligned = activation->ffi_struct_result;
	} else {
		requested_capacity = activation->ffi_struct_result_capacity > type->size
								 ? activation->ffi_struct_result_capacity
								 : type->size;
		if (alignment - 1 > SIZE_MAX - requested_capacity) {
			return false;
		}
		allocation_size = requested_capacity + alignment - 1;
		allocation = malloc(allocation_size);
		if (allocation == NULL) {
			return false;
		}
		address = (uintptr_t)allocation;
		remainder = address % alignment;
		aligned = (void*)(address + (remainder == 0 ? 0 : alignment - remainder));
		free(activation->ffi_struct_result_allocation);
		activation->ffi_struct_result_allocation = allocation;
		activation->ffi_struct_result = aligned;
		activation->ffi_struct_result_capacity = requested_capacity;
	}
	out_result->pt = aligned;
	out_result->min = aligned;
	out_result->max = (char*)aligned + type->size - 1;
	return true;
}

static void po_ffi_call_release(Po_FFI_Call* call)
{
	unsigned int index;

	for (index = 0; index < call->arg_count; ++index) {
		free(call->struct_arguments != NULL ? call->struct_arguments[index] : NULL);
	}
	free(call->args);
	free(call->arg_types);
	free(call->data);
	free(call->pointer_arguments);
	free(call->struct_arguments);
	memset(call, 0, sizeof(*call));
}

static bool po_ffi_call_allocate(Po_FFI_Call* call, unsigned int arg_count)
{
	const size_t count = (size_t)arg_count;
	size_t slots;

	if (count == SIZE_MAX) {
		return false;
	}
	slots = count + 1; /* retain a checked trailing NULL slot for libffi. */
	if (slots > SIZE_MAX / sizeof(*call->args) || slots > SIZE_MAX / sizeof(*call->arg_types) ||
		slots > SIZE_MAX / sizeof(*call->data) ||
		slots > SIZE_MAX / sizeof(*call->pointer_arguments) ||
		slots > SIZE_MAX / sizeof(*call->struct_arguments)) {
		return false;
	}

	call->args = calloc(slots, sizeof(*call->args));
	call->arg_types = calloc(slots, sizeof(*call->arg_types));
	call->data = calloc(slots, sizeof(*call->data));
	call->pointer_arguments = calloc(slots, sizeof(*call->pointer_arguments));
	call->struct_arguments = calloc(slots, sizeof(*call->struct_arguments));
	if (call->args == NULL || call->arg_types == NULL || call->data == NULL ||
		call->pointer_arguments == NULL || call->struct_arguments == NULL) {
		po_ffi_call_release(call);
		return false;
	}
	call->arg_count = arg_count;
	return true;
}

static Po_FFI_Call* po_ffi_activation_call_find(PocoActivation* activation, const Po_FFI* binding)
{
	Po_FFI_Activation_Call* cached;

	if (activation == NULL) {
		return NULL;
	}
	for (cached = activation->ffi_calls; cached != NULL; cached = cached->next) {
		if (cached->binding == binding) {
			return &cached->call;
		}
	}
	return NULL;
}

Errcode po_ffi_activation_calls_create(PocoActivation* activation)
{
	Po_FuncMap* func_map;
	Po_FFI_Activation_Call* cached;
	Po_FFI* binding;
	void* key;
	unsigned int index;
	unsigned int context_count;
	Popot ignored_result;

	if (activation == NULL || activation->code == NULL) {
		return Err_null_ref;
	}
	func_map = (Po_FuncMap*)activation->code->ffi_bindings;
	if (func_map == NULL) {
		return Success;
	}
	hashmap_foreach(key, binding, &func_map->map)
	{
		(void)key;
		if (po_ffi_is_variadic(binding)) {
			continue;
		}
		context_count = (binding->flags & PO_FFI_RUN_CONTEXT) != 0 ? 1u : 0u;
		cached = calloc(1, sizeof(*cached));
		if (cached == NULL ||
			!po_ffi_call_allocate(&cached->call, binding->arg_count + context_count)) {
			free(cached);
			po_ffi_activation_calls_release(activation);
			return Err_no_memory;
		}
		cached->binding = binding;
		for (index = 0; index < binding->arg_count; ++index) {
			cached->call.arg_types[index] = binding->arg_types[index];
			if (binding->arg_ido_types[index] == IDO_STRUCT) {
				cached->call.struct_arguments[index] = malloc(binding->arg_types[index]->size);
				if (cached->call.struct_arguments[index] == NULL) {
					po_ffi_call_release(&cached->call);
					free(cached);
					po_ffi_activation_calls_release(activation);
					return Err_no_memory;
				}
			}
		}
		if (context_count != 0) {
			cached->call.arg_types[binding->arg_count] = &ffi_type_pointer;
		}
		cached->next = activation->ffi_calls;
		activation->ffi_calls = cached;
		if (binding->result_ido_type == IDO_STRUCT &&
			!po_ffi_struct_result_prepare(activation, binding->result_type, &ignored_result)) {
			po_ffi_activation_calls_release(activation);
			return Err_no_memory;
		}
	}
	return Success;
}

void po_ffi_activation_calls_reset(PocoActivation* activation)
{
	Po_FFI_Activation_Call* cached;

	if (activation == NULL) {
		return;
	}
	for (cached = activation->ffi_calls; cached != NULL; cached = cached->next) {
		memset(cached->call.data, 0,
			   ((size_t)cached->call.arg_count + 1) * sizeof(*cached->call.data));
		memset(cached->call.pointer_arguments, 0,
			   ((size_t)cached->call.arg_count + 1) * sizeof(*cached->call.pointer_arguments));
		memset(&cached->call.result, 0, sizeof(cached->call.result));
	}
	activation->ffi_fixed_call_prep_count = 0;
	activation->ffi_variadic_call_prep_count = 0;
	activation->ffi_per_call_allocation_count = 0;
	activation->ffi_fixed_call_cache_hit_count = 0;
}

void po_ffi_activation_calls_release(PocoActivation* activation)
{
	Po_FFI_Activation_Call* cached;

	if (activation == NULL) {
		return;
	}
	while (activation->ffi_calls != NULL) {
		cached = activation->ffi_calls;
		activation->ffi_calls = cached->next;
		po_ffi_call_release(&cached->call);
		free(cached);
	}
}

static bool po_ffi_contract_scalar(const Po_FFI* binding, const Po_FFI_Call* call,
								   size_t parameter_index, size_t* out_value)
{
	if (out_value == NULL) {
		return false;
	}
	if (parameter_index == POCO_BINDING_PARAMETER_NONE) {
		*out_value = 1;
		return true;
	}
	if (!po_ffi_contract_index_is_scalar(binding, parameter_index)) {
		return false;
	}
	if (binding->arg_ido_types[parameter_index] == IDO_INT) {
		if (call->data[parameter_index].i < 0) {
			return false;
		}
		*out_value = (size_t)call->data[parameter_index].i;
	} else {
		if (call->data[parameter_index].l < 0) {
			return false;
		}
		*out_value = (size_t)call->data[parameter_index].l;
	}
	return true;
}

static bool po_ffi_popot_capacity(const Popot* pointer, size_t* out_capacity)
{
	uintptr_t current;
	uintptr_t minimum;
	uintptr_t maximum;

	if (pointer == NULL || out_capacity == NULL || pointer->pt == NULL || pointer->min == NULL ||
		pointer->max == NULL) {
		return false;
	}
	minimum = (uintptr_t)pointer->min;
	current = (uintptr_t)pointer->pt;
	maximum = (uintptr_t)pointer->max;
	if (minimum > current || current > maximum || maximum - current == UINTPTR_MAX) {
		return false;
	}
	*out_capacity = (size_t)(maximum - current + 1);
	return true;
}

static bool po_ffi_cstring_length(PocoPointerRegistry* pointer_registry, const Popot* pointer,
								  size_t* out_length)
{
	size_t capacity;
	const char* terminator;

	if (!po_ffi_popot_capacity(pointer, &capacity) ||
		!poco_pointer_registry_validate(pointer_registry, pointer, capacity,
										POCO_POINTER_PERMISSION_READ)) {
		return false;
	}
	terminator = memchr(pointer->pt, '\0', capacity);
	if (terminator == NULL) {
		return false;
	}
	*out_length = (size_t)(terminator - (const char*)pointer->pt) + 1;
	return true;
}

static bool po_ffi_contract_span_length(const Po_FFI* binding, const Po_FFI_Call* call,
										PocoPointerRegistry* pointer_registry,
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

	if (out_length == NULL) {
		return false;
	}
	switch (span_kind) {
		case POCO_BINDING_SPAN_BYTES:
			if (!po_ffi_contract_scalar(binding, call, byte_count_parameter, &first_factor) ||
				!po_ffi_contract_scalar(binding, call, element_count_parameter, &second_factor) ||
				byte_count != 0 && first_factor > SIZE_MAX / byte_count) {
				return false;
			}
			first_factor *= byte_count;
			if (second_factor != 0 && first_factor > SIZE_MAX / second_factor) {
				return false;
			}
			*out_length = first_factor * second_factor;
			return true;
		case POCO_BINDING_SPAN_C_STRING:
			source_parameter = string_parameter == POCO_BINDING_PARAMETER_NONE
								   ? fallback_pointer_parameter
								   : string_parameter;
			if (!po_ffi_contract_index_is_pointer(binding, source_parameter)) {
				return false;
			}
			return po_ffi_cstring_length(pointer_registry,
										 &call->pointer_arguments[source_parameter], out_length);
		case POCO_BINDING_SPAN_APPEND_C_STRING:
			if (!po_ffi_contract_index_is_pointer(binding, fallback_pointer_parameter) ||
				!po_ffi_contract_index_is_pointer(binding, string_parameter) ||
				!po_ffi_cstring_length(pointer_registry,
									   &call->pointer_arguments[fallback_pointer_parameter],
									   &destination_length) ||
				!po_ffi_cstring_length(pointer_registry, &call->pointer_arguments[string_parameter],
									   &source_length) ||
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

	if (contract == NULL) {
		return true;
	}
	for (index = 0; index < contract->pointer_contract_count; ++index) {
		const PocoBindingPointerContract* pointer = &contract->pointer_contracts[index];
		size_t byte_count;

		if (!po_ffi_contract_span_length(
				binding, call, pointer_registry, pointer->span_kind, pointer->byte_count,
				pointer->byte_count_parameter, pointer->element_count_parameter,
				pointer->string_parameter, pointer->parameter_index, &byte_count) ||
			!poco_pointer_registry_validate(pointer_registry,
											&call->pointer_arguments[pointer->parameter_index],
											byte_count, pointer->permissions)) {
			return false;
		}
	}
	return true;
}

static bool po_ffi_apply_pointer_return(const Po_FFI* binding, const Po_FFI_Call* call,
										PocoPointerRegistry* pointer_registry, void* raw_pointer,
										Popot* out_pointer)
{
	const PocoBindingReturnContract* contract;
	uintptr_t raw;

	if (out_pointer == NULL) {
		return false;
	}
	Popot_make_null(out_pointer);
	if (raw_pointer == NULL) {
		return true;
	}
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

		if (alias->min == NULL || alias->max == NULL) {
			return false;
		}
		raw = (uintptr_t)raw_pointer;
		minimum = (uintptr_t)alias->min;
		maximum = (uintptr_t)alias->max;
		if (raw < minimum || raw > maximum) {
			return false;
		}
		*out_pointer = *alias;
		out_pointer->pt = raw_pointer;
		return true;
	}
	if (contract->origin == POCO_POINTER_RETURN_BORROWED) {
		uint32_t permissions;

		if (!poco_pointer_registry_find(pointer_registry, raw_pointer, out_pointer, &permissions) ||
			(permissions & contract->permissions) != contract->permissions) {
			return false;
		}
		return true;
	}
	if (contract->origin == POCO_POINTER_RETURN_OWNED) {
		size_t byte_count;
		uintptr_t start;
		uintptr_t end;

		if (!po_ffi_contract_span_length(
				binding, call, pointer_registry, contract->span_kind, contract->byte_count,
				contract->byte_count_parameter, contract->element_count_parameter,
				contract->string_parameter, POCO_BINDING_PARAMETER_NONE, &byte_count) ||
			!poco_pointer_span_range(raw_pointer, byte_count, &start, &end)) {
			return false;
		}
		if (pointer_registry != NULL &&
			poco_pointer_registry_register_owned(pointer_registry, raw_pointer, byte_count,
												 contract->permissions, contract->release,
												 contract->release_user_data) != Success) {
			if (contract->release != NULL) {
				contract->release(raw_pointer, contract->release_user_data);
			} else {
				free(raw_pointer);
			}
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
				   const Po_FFI_Variadic_Descriptor* variadic, PocoActivation* env)
{
	Pt_num result;
	Po_FFI_Call local_call = {0};
	Po_FFI_Call* call;
	const Pt_num* stack = stack_in;
	PocoPointerRegistry* pointer_registry = env != NULL ? env->pointer_registry : NULL;
	Errcode ignored_error = Success;
	Errcode* builtin_error = env != NULL ? &env->builtin_error : &ignored_error;
	unsigned int variadic_count = 0;
	unsigned int total_count;
	unsigned int context_count;
	bool is_variadic;
	bool is_activation;
	bool release_call = false;
	ffi_status status;
	unsigned int index;

	Popot_make_null(&result.ppt);
	if (binding == NULL) {
		(*builtin_error) = Err_poco_ffi_func_not_found;
		return result;
	}

	is_variadic = po_ffi_is_variadic(binding);
	is_activation = env != NULL && env->ffi_activation_magic == UINT64_C(0x504f434f46464941);
	context_count = (binding->flags & PO_FFI_RUN_CONTEXT) != 0 ? 1u : 0u;
	if (context_count != 0 && (env == NULL || env->vm == NULL)) {
		(*builtin_error) = Err_poco_ffi_invalid_binding;
		return result;
	}
	if (is_variadic) {
		if (variadic == NULL || (variadic->count != 0 && variadic->types == NULL)) {
			(*builtin_error) = Err_poco_ffi_invalid_binding;
			return result;
		}
		if (variadic->count > UINT_MAX - binding->arg_count) {
			(*builtin_error) = Err_poco_ffi_variadic_overflow;
			return result;
		}
		variadic_count = variadic->count;
	}
	if (!is_variadic && binding->arg_count != 0 && stack == NULL) {
		(*builtin_error) = Err_poco_ffi_invalid_binding;
		return result;
	}

	if (binding->arg_count > UINT_MAX - variadic_count - context_count) {
		(*builtin_error) = Err_poco_ffi_variadic_overflow;
		return result;
	}
	total_count = binding->arg_count + variadic_count + context_count;
	if (is_variadic) {
		if (is_activation) {
			++env->ffi_per_call_allocation_count;
		}
		if (!po_ffi_call_allocate(&local_call, total_count)) {
			(*builtin_error) = Err_no_memory;
			return result;
		}
		call = &local_call;
		release_call = true;
	} else {
		call = is_activation ? po_ffi_activation_call_find(env, binding) : NULL;
		if (call == NULL) {
			/* Private descriptor tests and legacy internal callers do not own a
			 * PocoActivation.  Keep their compatibility path dynamic; compiled
			 * programs always use the activation cache. */
			if (is_activation) {
				++env->ffi_per_call_allocation_count;
			}
			if (!po_ffi_call_allocate(&local_call, total_count)) {
				(*builtin_error) = Err_no_memory;
				return result;
			}
			call = &local_call;
			release_call = true;
		} else if (call->arg_count != total_count) {
			(*builtin_error) = Err_poco_ffi_invalid_binding;
			return result;
		} else {
			/* The cached call buffers and prepared cif are reused as-is. */
			++env->ffi_fixed_call_cache_hit_count;
		}
	}

	for (index = 0; index < binding->arg_count; ++index) {
		size_t struct_size;
		size_t struct_capacity;

		call->arg_types[index] = binding->arg_types[index];
		switch (binding->arg_ido_types[index]) {
			case IDO_INT:
				call->data[index].i = stack->i;
				call->args[index] = &call->data[index].i;
				stack = (const Pt_num*)OPTR(stack, sizeof(int));
				break;
			case IDO_LONG:
				call->data[index].l = stack->l;
				call->args[index] = &call->data[index].l;
				stack = (const Pt_num*)OPTR(stack, sizeof(long));
				break;
			case IDO_DOUBLE:
				if (binding->arg_types[index] == &ffi_type_float) {
					call->data[index].f = (float)stack->d;
					call->args[index] = &call->data[index].f;
				} else {
					call->data[index].d = stack->d;
					call->args[index] = &call->data[index].d;
				}
				stack = (const Pt_num*)OPTR(stack, sizeof(double));
				break;
			case IDO_POINTER:
				call->pointer_arguments[index] = stack->ppt;
				call->data[index].p = stack->ppt.pt;
				call->args[index] = &call->data[index].p;
				stack = (const Pt_num*)OPTR(stack, sizeof(Popot));
				break;
			case IDO_CPT:
				call->data[index].p = stack->p;
				call->args[index] = &call->data[index].p;
				stack = (const Pt_num*)OPTR(stack, sizeof(void*));
				break;
			case IDO_STRUCT:
				struct_size =
					binding->arg_types[index] != NULL ? binding->arg_types[index]->size : 0;
				if (struct_size == 0 || !po_ffi_popot_capacity(&stack->ppt, &struct_capacity) ||
					struct_capacity < struct_size) {
					poco_set_error(env != NULL ? env->vm : NULL,
								   "FFI call '%s' struct argument exceeds its Poco bounds.",
								   binding->name);
					(*builtin_error) = Err_poco_ffi_bounds;
					goto CLEANUP;
				}
				if (release_call && call->struct_arguments[index] == NULL) {
					call->struct_arguments[index] = malloc(struct_size);
				}
				if (call->struct_arguments[index] == NULL) {
					(*builtin_error) = Err_no_memory;
					goto CLEANUP;
				}
				memcpy(call->struct_arguments[index], stack->ppt.pt, struct_size);
				call->args[index] = call->struct_arguments[index];
				stack = (const Pt_num*)OPTR(stack, sizeof(Popot));
				break;
			default:
				(*builtin_error) = Err_poco_ffi_invalid_binding;
				goto CLEANUP;
		}
	}
	if (!po_ffi_validate_contract(binding, call, pointer_registry)) {
		poco_set_error(env != NULL ? env->vm : NULL,
					   "FFI call '%s' exceeds its contracted pointer span.", binding->name);
		(*builtin_error) = Err_poco_ffi_bounds;
		goto CLEANUP;
	}

	if (context_count != 0) {
		const unsigned int context_index = binding->arg_count;

		call->data[context_index].p = env->vm;
		call->args[context_index] = &call->data[context_index].p;
		call->arg_types[context_index] = &ffi_type_pointer;
	}

	for (index = 0; index < variadic_count; ++index) {
		const ffi_type* type = variadic->types[index];
		const size_t size = type == NULL ? 0 : po_ffi_argtype_size(type);
		const unsigned int call_index = binding->arg_count + context_count + index;

		if (size == 0 || size > sizeof(call->data[call_index])) {
			(*builtin_error) = Err_poco_ffi_invalid_binding;
			goto CLEANUP;
		}
		memcpy(&call->data[call_index], stack, size);
		call->args[call_index] = &call->data[call_index];
		call->arg_types[call_index] = (ffi_type*)type;
		stack = (const Pt_num*)OPTR(stack, size);
	}

	call->args[call->arg_count] = NULL;
	call->arg_types[call->arg_count] = NULL;
	if (is_variadic) {
		status = po_ffi_prepare_variadic_cif(is_activation ? env : NULL, &call->interface,
											 binding->arg_count + context_count, call->arg_count,
											 binding->result_type, call->arg_types);
	} else {
		status = FFI_OK;
	}
	if (status != FFI_OK) {
		poco_set_error(env != NULL ? env->vm : NULL,
					   "FFI call '%s' could not prepare its call interface: %s.", binding->name,
					   po_ffi_status_str(status));
		(*builtin_error) = Err_poco_ffi_invalid_binding;
		goto CLEANUP;
	}
	if (binding->result_ido_type == IDO_STRUCT &&
		!po_ffi_struct_result_prepare(env, binding->result_type, &result.ppt)) {
		poco_set_error(env != NULL ? env->vm : NULL,
					   "FFI call '%s' could not allocate its struct return temp.", binding->name);
		(*builtin_error) = Err_no_memory;
		goto CLEANUP;
	}

	ffi_call(is_variadic ? &call->interface : (ffi_cif*)&binding->interface,
			 FFI_FN(binding->function),
			 binding->result_ido_type == IDO_VOID
				 ? NULL
				 : (binding->result_ido_type == IDO_STRUCT ? result.ppt.pt : &call->result),
			 call->args);
	switch (binding->result_ido_type) {
		case IDO_INT:
			result.i = call->result.i;
			break;
		case IDO_LONG:
			result.l = call->result.l;
			break;
		case IDO_DOUBLE:
			if (binding->result_type == &ffi_type_float) {
				result.d = (double)call->result.f;
			} else {
				result.d = call->result.d;
			}
			break;
		case IDO_POINTER:
			if (!po_ffi_apply_pointer_return(binding, call, pointer_registry, call->result.p,
											 &result.ppt)) {
				poco_set_error(env != NULL ? env->vm : NULL,
							   "FFI call '%s' returned a pointer outside its contract.",
							   binding->name);
				(*builtin_error) = Err_poco_ffi_bounds;
			}
			break;
		case IDO_CPT:
			result.p = call->result.p;
			break;
		case IDO_STRUCT:
			/* result.ppt already describes the correctly aligned activation temp. */
			break;
		default:
			break;
	}

CLEANUP:
	if (release_call) {
		po_ffi_call_release(&local_call);
	}
	return result;
}
