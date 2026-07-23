#include "activation.h"

/* C99-compatible compile-time assertions.  The Poco target deliberately uses
 * GNU extensions, so typeof gives this structural check the exact declared
 * field type instead of accepting C's mutable-to-const pointer conversion. */
#define POCO_TYPE_IS(expression, expected_type) \
	__builtin_types_compatible_p(__typeof__(expression), expected_type)
#define POCO_STATIC_ASSERT(name, condition) \
	typedef char poco_static_assert_##name[(condition) ? 1 : -1]

POCO_STATIC_ASSERT(functions_are_const,
				   POCO_TYPE_IS(((Poco_program_code*)0)->functions, const Func_frame*));
POCO_STATIC_ASSERT(literals_are_const,
				   POCO_TYPE_IS(((Poco_program_code*)0)->literals, const Names*));
POCO_STATIC_ASSERT(prototypes_are_const,
				   POCO_TYPE_IS(((Poco_program_code*)0)->prototypes, const Func_frame*));
POCO_STATIC_ASSERT(ffi_bindings_are_const,
				   POCO_TYPE_IS(((Poco_program_code*)0)->ffi_bindings, const Po_FuncMap*));
POCO_STATIC_ASSERT(builtin_libraries_are_const,
				   POCO_TYPE_IS(((Poco_program_code*)0)->builtin_libraries, const Poco_lib*));
POCO_STATIC_ASSERT(loaded_libraries_are_const,
				   POCO_TYPE_IS(((Poco_program_code*)0)->loaded_libraries, const Poco_lib*));
POCO_STATIC_ASSERT(program_libraries_are_const,
				   POCO_TYPE_IS(((Poco_program_code*)0)->program_libraries,
								const Poco_program_library*));
POCO_STATIC_ASSERT(allocation_owner_is_const,
				   POCO_TYPE_IS(((Poco_program_code*)0)->allocation_owner, const void*));
POCO_STATIC_ASSERT(program_reference_is_const,
				   POCO_TYPE_IS(((PocoActivation*)0)->program, const PocoProgram*));

/*
 * The mutable per-run categories must each name a distinct field on
 * PocoActivation. Referencing every one keeps this list honest: if the split in
 * 03-01b ever moves a mutable category back onto the shared Poco_program_code
 * (or drops it from the activation), this translation unit stops compiling.
 * Runtime state independence itself is covered by the concurrency harness.
 */
POCO_STATIC_ASSERT(activation_owns_stack, POCO_TYPE_IS(((PocoActivation*)0)->stack, char*));
POCO_STATIC_ASSERT(activation_owns_data, POCO_TYPE_IS(((PocoActivation*)0)->data, char*));
POCO_STATIC_ASSERT(activation_owns_main_argv,
				   POCO_TYPE_IS(((PocoActivation*)0)->main_argv_allocation, void*));
POCO_STATIC_ASSERT(activation_owns_variadic,
				   POCO_TYPE_IS(((PocoActivation*)0)->variadic, Po_FFI_Variadic_Descriptor));
POCO_STATIC_ASSERT(activation_owns_callback_frames,
				   POCO_TYPE_IS(((PocoActivation*)0)->callback_frames, Func_frame*));
POCO_STATIC_ASSERT(activation_owns_builtin_libraries,
				   POCO_TYPE_IS(((PocoActivation*)0)->builtin_libraries, Poco_lib*));
POCO_STATIC_ASSERT(activation_owns_loaded_libraries,
				   POCO_TYPE_IS(((PocoActivation*)0)->loaded_libraries, Poco_lib*));
POCO_STATIC_ASSERT(activation_owns_program_libraries,
				   POCO_TYPE_IS(((PocoActivation*)0)->program_libraries, Poco_program_library*));
POCO_STATIC_ASSERT(activation_owns_result, POCO_TYPE_IS(((PocoActivation*)0)->result, Pt_num));

#undef POCO_STATIC_ASSERT
#undef POCO_TYPE_IS

int main(void)
{
	/* Every guarantee in this file is enforced at compile time by the static
	 * assertions above; a successful build is the test passing. */
	return 0;
}
