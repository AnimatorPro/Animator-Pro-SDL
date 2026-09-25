/*
 * function_signature - reading a compiled function's shape before calling it.
 *
 * A host that loads a .pex it did not compile needs to know whether a handler
 * still has the arity and types it was written against.  The call path already
 * compares those internally, but only once the host has committed to the call
 * and only as a single undifferentiated range error.  These cases pin down the
 * read-only accessors that answer the same question at load time, and the one
 * property that makes them usable: a struct's identity is its table id, never
 * its tag.
 */

#include <poco/poco.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef POCO_FUNCTION_SIGNATURE_DIR
#error "POCO_FUNCTION_SIGNATURE_DIR must name the function-signature fixture directory"
#endif

/* Poco packs struct members with no alignment padding: the loader accumulates
 * member sizes in declaration order, so a host comparing struct_size against
 * its own sizeof must pack its side too. */
#define VEC_PACKED_SIZE (sizeof(int) + sizeof(double))

static int failures;

static char path_storage[4][1024];

#define CHECK(condition, message)                                    \
	do {                                                             \
		if (!(condition)) {                                          \
			fprintf(stderr, "poco_function_signature: %s\n", message); \
			++failures;                                              \
		}                                                            \
	} while (0)

static const char* fixture(size_t slot, const char* name)
{
	snprintf(path_storage[slot], sizeof(path_storage[slot]), "%s%s",
			 POCO_FUNCTION_SIGNATURE_DIR, name);
	return path_storage[slot];
}

static int host_probe(int a)
{
	return a;
}

static const PocoBinding host_bindings[] = {
	{"int host_probe(int a);", (PocoNativeFunction)host_probe, NULL, 0},
};

static const PocoLibrary host_library = {
	"function-signature-test",
	host_bindings,
	sizeof(host_bindings) / sizeof(host_bindings[0]),
	NULL,
	NULL,
	NULL,
};

static PocoVm* make_vm(void)
{
	PocoVm* vm = NULL;

	CHECK(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "VM creation failed");
	if (vm != NULL) {
		CHECK(poco_vm_register_library(vm, &host_library) == POCO_STATUS_OK,
			  "host binding registration failed");
	}
	return vm;
}

static PocoProgram* compile(PocoVm* vm, const char* const* names, size_t count)
{
	PocoProgram* program = NULL;

	if (poco_vm_compile_files(vm, names, count, &program) != POCO_STATUS_OK) {
		fprintf(stderr, "poco_function_signature: compile failed: %s\n", poco_get_last_error(vm));
		++failures;
		return NULL;
	}
	return program;
}

/*----------------------------------------------------------------------------
 * The shapes a host actually asks about.
 *--------------------------------------------------------------------------*/

static void test_scalar_and_struct_parameters(PocoActivation* activation)
{
	PocoFunctionSignature signature;
	PocoTypeDesc type;
	const char* name = NULL;

	CHECK(poco_activation_function_signature(activation, "measure", &signature) == POCO_STATUS_OK,
		  "measure has no signature");
	CHECK(signature.parameter_count == 3, "measure does not report three parameters");
	CHECK(!signature.is_native, "a Poco function reports as native");
	CHECK(strcmp(signature.name, "measure") == 0, "measure reports the wrong name");
	CHECK(signature.return_type.kind == POCO_CALLBACK_VALUE_INT, "measure does not return int");
	CHECK(!signature.return_type.is_struct, "an int return claims to be a struct");

	CHECK(poco_activation_function_parameter(activation, "measure", 0, &name, &type) ==
			  POCO_STATUS_OK,
		  "measure parameter 0 is unreadable");
	CHECK(strcmp(name, "a") == 0, "parameter 0 is not the first declared");
	CHECK(type.kind == POCO_CALLBACK_VALUE_INT, "an int parameter is not int");
	CHECK(type.struct_id == POCO_STRUCT_NONE, "a scalar carries a struct id");

	/* float is promoted, so the host exchanges a double.  This asserts the
	 * behaviour rather than the C declaration. */
	CHECK(poco_activation_function_parameter(activation, "measure", 1, &name, &type) ==
			  POCO_STATUS_OK,
		  "measure parameter 1 is unreadable");
	CHECK(strcmp(name, "b") == 0, "parameter 1 is not the second declared");
	CHECK(type.kind == POCO_CALLBACK_VALUE_DOUBLE, "a float parameter is not reported as double");

	CHECK(poco_activation_function_parameter(activation, "measure", 2, &name, &type) ==
			  POCO_STATUS_OK,
		  "measure parameter 2 is unreadable");
	CHECK(strcmp(name, "c") == 0, "parameter 2 is not the third declared");
	CHECK(type.kind == POCO_CALLBACK_VALUE_POPOT, "a script pointer is not a popot");
	CHECK(type.is_struct, "a pointer to a struct does not name its struct");
	CHECK(type.struct_name != NULL && strcmp(type.struct_name, "vec") == 0,
		  "the struct parameter names the wrong tag");
	CHECK(type.struct_size == VEC_PACKED_SIZE, "struct vec has an unexpected packed size");
	CHECK(type.struct_member_count == 2, "struct vec does not report two members");
	CHECK(type.struct_id != POCO_STRUCT_NONE, "the struct parameter has no table id");
}

static void test_return_types(PocoActivation* activation)
{
	PocoFunctionSignature signature;

	CHECK(poco_activation_function_signature(activation, "nothing", &signature) == POCO_STATUS_OK,
		  "nothing has no signature");
	CHECK(signature.parameter_count == 1, "nothing does not report one parameter");
	CHECK(signature.return_type.kind == POCO_CALLBACK_VALUE_INVALID,
		  "a void return is not reported as having no host value");
	CHECK(!signature.return_type.is_struct, "a void return claims to be a struct");

	CHECK(poco_activation_function_signature(activation, "widen", &signature) == POCO_STATUS_OK,
		  "widen has no signature");
	CHECK(signature.parameter_count == 0, "widen does not report zero parameters");
	CHECK(signature.return_type.kind == POCO_CALLBACK_VALUE_LONG, "widen does not return long");

	CHECK(poco_activation_function_signature(activation, "pass_through", &signature) ==
			  POCO_STATUS_OK,
		  "pass_through has no signature");
	CHECK(signature.return_type.kind == POCO_CALLBACK_VALUE_POPOT,
		  "a struct pointer return is not a popot");
	CHECK(signature.return_type.is_struct, "a struct pointer return does not name its struct");
	CHECK(signature.return_type.struct_name != NULL &&
			  strcmp(signature.return_type.struct_name, "vec") == 0,
		  "the struct pointer return names the wrong tag");
}

/* A union is tabled beside structs and answers the same way. */
static void test_union_parameter(PocoActivation* activation)
{
	PocoTypeDesc type;

	CHECK(poco_activation_function_parameter(activation, "scale", 0, NULL, &type) ==
			  POCO_STATUS_OK,
		  "scale parameter 0 is unreadable");
	CHECK(type.is_struct, "a pointer to a union does not name its layout");
	CHECK(type.struct_name != NULL && strcmp(type.struct_name, "slot") == 0,
		  "the union parameter names the wrong tag");
	CHECK(type.struct_member_count == 2, "union slot does not report two members");
	CHECK(type.struct_size == sizeof(double), "a union is not sized by its widest member");
}

/* Only Poco functions reach the compiled-function list; a native has a frame
 * and nothing else, and a host must still be able to ask about it. */
static void test_native_functions(PocoActivation* activation)
{
	PocoFunctionSignature signature;

	CHECK(poco_activation_function_signature(activation, "host_probe", &signature) ==
			  POCO_STATUS_OK,
		  "a host-provided native has no signature");
	CHECK(signature.is_native, "a host-provided native does not report as native");
	CHECK(signature.parameter_count == 1, "host_probe does not report one parameter");
	CHECK(signature.return_type.kind == POCO_CALLBACK_VALUE_INT, "host_probe does not return int");
}

static void test_absent_and_out_of_range(PocoActivation* activation)
{
	PocoFunctionSignature signature;
	PocoTypeDesc type;
	const char* name = "untouched";

	memset(&signature, 0xA5, sizeof(signature));
	CHECK(poco_activation_function_signature(activation, "no_such_function", &signature) ==
			  POCO_STATUS_NOT_FOUND,
		  "an absent function is not reported as missing");
	{
		PocoFunctionSignature untouched;
		memset(&untouched, 0xA5, sizeof(untouched));
		CHECK(memcmp(&signature, &untouched, sizeof(signature)) == 0,
			  "a missing function wrote to the output signature");
	}

	memset(&type, 0xA5, sizeof(type));
	CHECK(poco_activation_function_parameter(activation, "no_such_function", 0, &name, &type) ==
			  POCO_STATUS_NOT_FOUND,
		  "an absent function's parameter is not reported as missing");
	CHECK(strcmp(name, "untouched") == 0, "a missing function wrote to the output name");

	CHECK(poco_activation_function_parameter(activation, "measure", 3, &name, &type) ==
			  POCO_STATUS_PARAMETER_RANGE,
		  "a parameter index past the last is not out of range");
	CHECK(poco_activation_function_parameter(activation, "widen", 0, &name, &type) ==
			  POCO_STATUS_PARAMETER_RANGE,
		  "a parameterless function accepts parameter 0");
}

static void test_struct_members(PocoActivation* activation, PocoProgram* program)
{
	PocoTypeDesc parameter;
	PocoTypeDesc member;
	const char* name = NULL;

	if (poco_activation_function_parameter(activation, "measure", 2, NULL, &parameter) !=
		POCO_STATUS_OK) {
		return;
	}
	CHECK(poco_program_struct_member(program, parameter.struct_id, 0, &name, &member) ==
			  POCO_STATUS_OK,
		  "struct vec member 0 is unreadable");
	CHECK(strcmp(name, "x") == 0, "struct members are not in declaration order");
	CHECK(member.kind == POCO_CALLBACK_VALUE_INT, "struct vec's first member is not int");

	CHECK(poco_program_struct_member(program, parameter.struct_id, 1, &name, &member) ==
			  POCO_STATUS_OK,
		  "struct vec member 1 is unreadable");
	CHECK(strcmp(name, "y") == 0, "struct members are not in declaration order");
	CHECK(member.kind == POCO_CALLBACK_VALUE_DOUBLE, "struct vec's second member is not double");

	CHECK(poco_program_struct_member(program, parameter.struct_id, 2, &name, &member) ==
			  POCO_STATUS_PARAMETER_RANGE,
		  "a member index past the last is not out of range");
	CHECK(poco_program_struct_member(program, POCO_STRUCT_NONE, 0, &name, &member) ==
			  POCO_STATUS_PARAMETER_RANGE,
		  "POCO_STRUCT_NONE names a struct");
	CHECK(poco_program_struct_member(program, 4096, 0, &name, &member) ==
			  POCO_STATUS_PARAMETER_RANGE,
		  "an id past the table names a struct");
}

/*----------------------------------------------------------------------------
 * The tag is not a key.
 *--------------------------------------------------------------------------*/

/*
 * Three units including one header is the normal multi-file case, and it is
 * not an error - but each unit mints its own layout, so one tag answers to
 * three ids of identical name and size.  A host keying off struct_name would
 * be comparing three different entries as though they were one.
 */
static void test_one_header_three_ids(void)
{
	PocoVm* vm = make_vm();
	PocoProgram* program;
	PocoActivation* activation = NULL;
	const char* names[3];
	PocoTypeDesc types[3];
	size_t index;

	if (vm == NULL) {
		return;
	}
	names[0] = fixture(0, "header0.poc");
	names[1] = fixture(1, "header1.poc");
	names[2] = fixture(2, "header2.poc");
	program = compile(vm, names, 3);
	if (program == NULL) {
		poco_vm_destroy(vm);
		return;
	}
	CHECK(poco_activation_acquire(program, &activation) == POCO_STATUS_OK,
		  "acquiring an activation for the shared-header program failed");
	if (activation != NULL) {
		static const char* const takers[3] = {"take0", "take1", "take2"};

		for (index = 0; index < 3; ++index) {
			CHECK(poco_activation_function_parameter(activation, takers[index], 0, NULL,
													 &types[index]) == POCO_STATUS_OK,
				  "a shared-header unit's parameter is unreadable");
		}
		for (index = 0; index < 3; ++index) {
			CHECK(types[index].struct_name != NULL &&
					  strcmp(types[index].struct_name, "vec") == 0,
				  "a shared-header unit renamed the tag");
			CHECK(types[index].struct_size == types[0].struct_size,
				  "identical header layouts disagree on size");
			CHECK(types[index].struct_id != POCO_STRUCT_NONE,
				  "a shared-header struct is missing from the table");
		}
		CHECK(types[0].struct_id != types[1].struct_id &&
				  types[1].struct_id != types[2].struct_id &&
				  types[0].struct_id != types[2].struct_id,
			  "three units including one header share a struct id");
		poco_activation_release(activation);
	}
	poco_program_destroy(program);
	poco_vm_destroy(vm);
}

/*----------------------------------------------------------------------------
 * The round trip.
 *--------------------------------------------------------------------------*/

static int same_type(const PocoTypeDesc* a, const PocoTypeDesc* b)
{
	if (a->kind != b->kind || a->is_struct != b->is_struct || a->struct_id != b->struct_id ||
		a->struct_size != b->struct_size || a->struct_member_count != b->struct_member_count) {
		return 0;
	}
	if ((a->struct_name == NULL) != (b->struct_name == NULL)) {
		return 0;
	}
	return a->struct_name == NULL || strcmp(a->struct_name, b->struct_name) == 0;
}

static void compare_signatures(PocoActivation* left, PocoActivation* right, const char* name)
{
	PocoFunctionSignature a;
	PocoFunctionSignature b;
	int index;

	if (poco_activation_function_signature(left, name, &a) != POCO_STATUS_OK ||
		poco_activation_function_signature(right, name, &b) != POCO_STATUS_OK) {
		CHECK(0, "a function is missing from one side of the round trip");
		return;
	}
	CHECK(strcmp(a.name, b.name) == 0, "a round-tripped function changed name");
	CHECK(a.parameter_count == b.parameter_count, "a round-tripped function changed arity");
	CHECK(a.is_native == b.is_native, "a round-tripped function changed frame kind");
	CHECK(same_type(&a.return_type, &b.return_type), "a round-tripped return type changed");
	for (index = 0; index < a.parameter_count && index < b.parameter_count; ++index) {
		PocoTypeDesc left_type;
		PocoTypeDesc right_type;
		const char* left_name = NULL;
		const char* right_name = NULL;

		CHECK(poco_activation_function_parameter(left, name, (size_t)index, &left_name,
												 &left_type) == POCO_STATUS_OK &&
				  poco_activation_function_parameter(right, name, (size_t)index, &right_name,
													 &right_type) == POCO_STATUS_OK,
			  "a round-tripped parameter is unreadable");
		CHECK(strcmp(left_name, right_name) == 0, "a round-tripped parameter changed name");
		CHECK(same_type(&left_type, &right_type), "a round-tripped parameter type changed");
	}
}

static uint8_t* serialize(PocoProgram* program, size_t* out_size)
{
	uint8_t* image;
	size_t size = 0;

	if (poco_program_serialize_buffer(program, POCO_DEBUG_LEVEL_MINIMAL, NULL, 0, &size) !=
			POCO_STATUS_BUFFER_TOO_SMALL ||
		size == 0) {
		CHECK(0, "serialized size query failed");
		return NULL;
	}
	image = malloc(size);
	if (image == NULL) {
		CHECK(0, "serialized image allocation failed");
		return NULL;
	}
	if (poco_program_serialize_buffer(program, POCO_DEBUG_LEVEL_MINIMAL, image, size, out_size) !=
		POCO_STATUS_OK) {
		CHECK(0, "serialization failed");
		free(image);
		return NULL;
	}
	return image;
}

static void test_round_trip(PocoVm* vm, PocoProgram* program, PocoActivation* activation)
{
	static const char* const functions[] = {"measure", "nothing",      "widen",
											"scale",   "pass_through", "host_probe"};
	PocoProgram* restored = NULL;
	PocoActivation* restored_activation = NULL;
	uint8_t* image;
	size_t size = 0;
	size_t index;

	image = serialize(program, &size);
	if (image == NULL) {
		return;
	}
	if (poco_vm_deserialize_buffer(vm, image, size, &restored) != POCO_STATUS_OK) {
		CHECK(0, "deserializing the program failed");
		free(image);
		return;
	}
	CHECK(poco_activation_acquire(restored, &restored_activation) == POCO_STATUS_OK,
		  "acquiring an activation for the restored program failed");
	if (restored_activation != NULL) {
		for (index = 0; index < sizeof(functions) / sizeof(functions[0]); ++index) {
			compare_signatures(activation, restored_activation, functions[index]);
		}
		poco_activation_release(restored_activation);
	}
	poco_program_destroy(restored);
	free(image);
}

int main(void)
{
	PocoVm* vm = make_vm();
	PocoProgram* program;
	PocoActivation* activation = NULL;
	const char* names[1];

	if (vm == NULL) {
		return 1;
	}
	names[0] = fixture(0, "shapes.poc");
	program = compile(vm, names, 1);
	if (program == NULL) {
		poco_vm_destroy(vm);
		return 1;
	}
	CHECK(poco_activation_acquire(program, &activation) == POCO_STATUS_OK,
		  "acquiring an activation failed");
	if (activation != NULL) {
		test_scalar_and_struct_parameters(activation);
		test_return_types(activation);
		test_union_parameter(activation);
		test_native_functions(activation);
		test_absent_and_out_of_range(activation);
		test_struct_members(activation, program);
		test_round_trip(vm, program, activation);
		poco_activation_release(activation);
	}
	poco_program_destroy(program);
	poco_vm_destroy(vm);

	test_one_header_three_ids();

	return failures == 0 ? 0 : 1;
}
