/*
 * poco_ffi.c - libffi bindings to compiled C functions, and the pointer
 * registry that validates Popot spans handed to them.
 */
#ifndef POCO_FFI_H
#define POCO_FFI_H

#include "poco_internal.h"

/*----------------------------------------------------------------------------
 * FFI structures -- bindings to compiled C functions
 *--------------------------------------------------------------------------*/

struct po_ffi_owned_type;
typedef struct po_ffi_owned_type Po_FFI_Owned_Type;
struct po_ffi_activation_call;

typedef union po_ffi_data /* Overlap popular datatypes in the same space */
{
	int i;
	short s;
	UBYTE* bpt;
	char c;
	long l;
	float f;
	double d;
	void* p;
} Po_FFI_Data;

/* Per-call variadic types are a dynamic FFI-owned descriptor.  It contains
 * only actual C variadic arguments: no stack count, byte size, or sentinel. */
typedef struct po_ffi_variadic_descriptor {
	ffi_type** types;
	unsigned int count;
	unsigned int capacity;
} Po_FFI_Variadic_Descriptor;

typedef struct po_ffi {
	ffi_cif interface;
	unsigned int arg_count;        // number of fixed native arguments
	ffi_type** arg_types;          // dynamically allocated libffi argument types
	IdoType* arg_ido_types;        // dynamically allocated original types
	uint32_t* arg_pointer_depths;  // declared pointer indirection for contracts
	ffi_type* result_type;
	IdoType result_ido_type;
	char* name;
	void* function;  // pointer to the actual function to call
	const PocoBindingContract* contract;
	uint64_t flags;
	Po_FFI_Owned_Type* owned_types;  // aggregate ffi_type storage, retained for cif lifetime
} Po_FFI;

enum { PO_FFI_VARIADIC = (1 << 0), PO_FFI_RUN_CONTEXT = (1 << 1) };

/*----------------------------------------------------------------------------
 * prototypes
 *--------------------------------------------------------------------------*/

ffi_type* po_ffi_type_from_ido_type(IdoType ido_type);
Errcode po_ffi_type_from_struct_info(Po_FFI* binding, const Struct_info* struct_info,
									 ffi_type** out_type);
const char* po_ffi_name_for_type(const ffi_type* type);
int po_ffi_build_structures(Poco_run_env* env);
void po_ffi_free_structures(Poco_run_env* env);
Po_FFI* po_ffi_find_binding(struct PocoActivation* activation, const void* key);
Po_FFI* po_ffi_find_binding_by_name(const Poco_run_env* env, const char* name);
Po_FFI* po_ffi_new(const C_frame* frame);
void po_ffi_delete(Po_FFI* binding);
void po_ffi_variadic_types_reset(Po_FFI_Variadic_Descriptor* variadic);
void po_ffi_variadic_types_release(Po_FFI_Variadic_Descriptor* variadic);
Errcode po_ffi_variadic_types_append(PocoVm* vm, Po_FFI_Variadic_Descriptor* variadic,
									 ffi_type* type);
Pt_num po_ffi_call(const Po_FFI* binding, const Pt_num* stack_in,
				   const Po_FFI_Variadic_Descriptor* variadic, struct PocoActivation* activation);
bool po_ffi_is_variadic(const Po_FFI* binding);

PocoPointerRegistry* poco_pointer_registry_create(void);
PocoPointerRegistry* poco_pointer_registry_create_child(PocoPointerRegistry* parent);
void poco_pointer_registry_destroy(PocoPointerRegistry* registry);
Errcode poco_pointer_registry_register_borrowed(PocoPointerRegistry* registry, void* pointer,
												size_t byte_count, uint32_t permissions);
Errcode poco_pointer_registry_unregister_borrowed(PocoPointerRegistry* registry,
												  const void* pointer);
Errcode poco_pointer_registry_register_owned(PocoPointerRegistry* registry, void* pointer,
											 size_t byte_count, uint32_t permissions,
											 PocoOwnedPointerRelease release,
											 void* release_user_data);
Errcode poco_pointer_registry_release_owned(PocoPointerRegistry* registry, const void* pointer);
bool poco_pointer_registry_validate(PocoPointerRegistry* registry, const Popot* pointer,
									size_t byte_count, uint32_t permissions);
bool poco_pointer_registry_is_managed(PocoPointerRegistry* registry, const Popot* pointer);
bool poco_pointer_registry_find(PocoPointerRegistry* registry, const void* pointer,
								Popot* out_pointer, uint32_t* out_permissions);

#endif /* POCO_FFI_H */
