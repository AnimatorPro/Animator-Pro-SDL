/*
 * host_native.c - '#pragma poco native' declarations.
 *
 * A declaration inside a native region compiles to a CFF_C frame with no
 * address, so a program can be compiled without the host that provides the
 * function and resolved by name when it is loaded.
 */

#include <poco/poco.h>

#include "poco_internal.h"
#include "program_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int host_add_calls;
static int host_read_calls;

#define CHECK(condition, message)                               \
	do {                                                        \
		if (!(condition)) {                                     \
			fprintf(stderr, "poco_host_native: %s\n", message); \
			++failures;                                         \
		}                                                       \
	} while (0)

static const char native_source[] =
	"#ifdef __POCO__\n"
	"#pragma poco native begin\n"
	"#endif\n"
	"int host_add(int a, int b);\n"
	"int host_read(char *bytes, int count);\n"
	"#ifdef __POCO__\n"
	"#pragma poco native end\n"
	"#endif\n"
	"main()\n"
	"{\n"
	"	return host_add(40, 2);\n"
	"}\n";

static int host_add(int a, int b)
{
	++host_add_calls;
	return a + b;
}

static int host_read(char* bytes, int count)
{
	++host_read_calls;
	return bytes != NULL && count != 0 ? (unsigned char)bytes[0] : 0;
}

static const PocoBindingPointerContract host_read_pointer_contracts[] = {
	{
		.parameter_index = 0,
		.permissions = POCO_POINTER_PERMISSION_READ,
		.pointer_depth = 1,
		.span_kind = POCO_BINDING_SPAN_BYTES,
		.byte_count = 1,
		.byte_count_parameter = 1,
		.element_count_parameter = POCO_BINDING_PARAMETER_NONE,
		.string_parameter = POCO_BINDING_PARAMETER_NONE,
	},
};

static const PocoBindingContract host_read_contract = {
	.pointer_contracts = host_read_pointer_contracts,
	.pointer_contract_count =
		sizeof(host_read_pointer_contracts) / sizeof(host_read_pointer_contracts[0]),
};

static const PocoBinding host_bindings[] = {
	{"int host_add(int a, int b);", (PocoNativeFunction)host_add, NULL, 0},
	{"int host_read(char *bytes, int count);", (PocoNativeFunction)host_read, &host_read_contract,
	 0},
};

static const PocoLibrary host_library = {
	"host-native-test",
	host_bindings,
	sizeof(host_bindings) / sizeof(host_bindings[0]),
	NULL,
	NULL,
	NULL,
};

static PocoVm* make_vm(int with_bindings)
{
	PocoVm* vm = NULL;

	CHECK(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "VM creation failed");
	if (vm != NULL && with_bindings) {
		CHECK(poco_vm_register_library(vm, &host_library) == POCO_STATUS_OK,
			  "host binding registration failed");
	}
	return vm;
}

static const Func_frame* find_frame(const PocoProgram* program, const char* name, int* out_count)
{
	const Poco_run_env* executable = program != NULL ? program->executable : NULL;
	const Func_frame* frame;
	const Func_frame* found = NULL;
	int count = 0;

	if (out_count != NULL) {
		*out_count = 0;
	}
	if (executable == NULL) {
		return NULL;
	}
	for (frame = executable->protos; frame != NULL; frame = frame->mlink) {
		if (frame->name != NULL && strcmp(frame->name, name) == 0) {
			++count;
			if (found == NULL) {
				found = frame;
			}
		}
	}
	if (out_count != NULL) {
		*out_count = count;
	}
	return found;
}

static PocoStatus compile_buffer(PocoVm* vm, const char* source, PocoProgram** out_program)
{
	return poco_vm_compile_buffer(vm, "host_native.poc", source, strlen(source), out_program);
}

static uint8_t* serialize(const PocoProgram* program, size_t* out_size)
{
	uint8_t* image = NULL;
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

/*
 * Compiled with nothing registered, a host-provided declaration is still a
 * native frame: CFF_C, with no address of its own.
 */
static uint8_t* test_compiles_without_the_host(size_t* out_size)
{
	PocoVm* vm = make_vm(0);
	PocoProgram* program = NULL;
	const Func_frame* frame;
	uint8_t* image = NULL;
	int count = 0;

	if (vm == NULL) {
		return NULL;
	}
	if (compile_buffer(vm, native_source, &program) != POCO_STATUS_OK) {
		CHECK(0, "compiling a host-provided native without the host failed");
		fprintf(stderr, "poco_host_native: %s\n", poco_get_last_error(vm));
		poco_vm_destroy(vm);
		return NULL;
	}
	frame = find_frame(program, "host_add", &count);
	CHECK(frame != NULL, "no frame was emitted for the host-provided native");
	if (frame != NULL) {
		CHECK(frame->type == CFF_C, "host-provided native is not a CFF_C frame");
		CHECK(frame->host_provided, "host-provided native lost its host-provided marking");
		CHECK(frame->code_pt == NULL, "host-provided native was bound at compile time");
		CHECK(frame->got_code, "host-provided native reads as a function with no code");
	}
	CHECK(count == 1, "host-provided native produced more than one frame");
	image = serialize(program, out_size);
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return image;
}

/* A VM that registers the binding resolves it and calls it with real values. */
static void test_resolves_against_a_registering_host(const uint8_t* image, size_t size)
{
	PocoVm* vm = make_vm(1);
	PocoProgram* program = NULL;
	const Func_frame* frame;
	int32_t result = 0;

	if (vm == NULL || image == NULL) {
		return;
	}
	host_add_calls = 0;
	CHECK(poco_vm_deserialize_buffer(vm, image, size, &program) == POCO_STATUS_OK,
		  "loading a host-provided native into a registering VM failed");
	if (program != NULL) {
		frame = find_frame(program, "host_add", NULL);
		CHECK(frame != NULL && frame->code_pt == (Code*)host_add,
			  "loaded native did not resolve to the registered function");
		CHECK(poco_vm_run(vm, program, NULL, &result) == POCO_STATUS_OK,
			  "running a resolved host-provided native failed");
		CHECK(result == 42, "host-provided native returned the wrong value");
		CHECK(host_add_calls == 1, "host-provided native did not reach the C function");
	}
	poco_program_destroy(program);
	poco_vm_destroy(vm);
}

/* A VM that does not provide it refuses the image at load, before any call. */
static void test_unprovided_native_is_refused_at_load(const uint8_t* image, size_t size)
{
	PocoVm* vm = make_vm(0);
	PocoProgram* program = NULL;

	if (vm == NULL || image == NULL) {
		return;
	}
	host_add_calls = 0;
	CHECK(
		poco_vm_deserialize_buffer(vm, image, size, &program) == POCO_STATUS_FFI_FUNCTION_NOT_FOUND,
		"an unprovided native was not refused at load");
	CHECK(program == NULL, "a refused image still produced a program");
	CHECK(host_add_calls == 0, "a refused image reached native code");
	poco_program_destroy(program);
	poco_vm_destroy(vm);
}

/* The declared prototype is what argument checking uses. */
static void test_wrong_argument_count_is_a_compile_error(void)
{
	static const char source[] =
		"#pragma poco native begin\n"
		"int host_add(int a, int b);\n"
		"#pragma poco native end\n"
		"main()\n"
		"{\n"
		"	return host_add(1);\n"
		"}\n";
	PocoVm* vm = make_vm(0);
	PocoProgram* program = NULL;

	if (vm == NULL) {
		return;
	}
	CHECK(compile_buffer(vm, source, &program) != POCO_STATUS_OK,
		  "calling a host-provided native with too few arguments compiled");
	CHECK(program == NULL, "a failed compile still produced a program");
	poco_program_destroy(program);
	poco_vm_destroy(vm);
}

/* The property this change most risks destroying. */
static void test_undeclared_function_still_fails(void)
{
	static const char missing[] =
		"main()\n"
		"{\n"
		"	return no_such_function(1);\n"
		"}\n";
	static const char declared_only[] =
		"int forgotten(int a);\n"
		"main()\n"
		"{\n"
		"	return forgotten(1);\n"
		"}\n";
	PocoVm* vm = make_vm(0);
	PocoProgram* program = NULL;

	if (vm == NULL) {
		return;
	}
	CHECK(compile_buffer(vm, missing, &program) != POCO_STATUS_OK,
		  "a call to an undeclared function compiled");
	CHECK(program == NULL, "a failed compile still produced a program");
	poco_program_destroy(program);
	program = NULL;
	CHECK(compile_buffer(vm, declared_only, &program) != POCO_STATUS_OK,
		  "a Poco function declared but never defined compiled");
	CHECK(program == NULL, "a failed compile still produced a program");
	poco_program_destroy(program);
	poco_vm_destroy(vm);
}

/*
 * The contract lives on the host's binding entry, so a contracted native
 * reached through a host-provided declaration is still bounds-checked.
 */
static void test_contract_survives_a_host_provided_declaration(void)
{
	static const char source[] =
		"#pragma poco native begin\n"
		"int host_read(char *bytes, int count);\n"
		"#pragma poco native end\n"
		"main()\n"
		"{\n"
		"	char bytes[4];\n"
		"	bytes[0] = 7;\n"
		"	return host_read(bytes, 5);\n"
		"}\n";
	static const char in_span[] =
		"#pragma poco native begin\n"
		"int host_read(char *bytes, int count);\n"
		"#pragma poco native end\n"
		"main()\n"
		"{\n"
		"	char bytes[4];\n"
		"	bytes[0] = 7;\n"
		"	return host_read(bytes, 4);\n"
		"}\n";
	PocoVm* vm = make_vm(0);
	PocoProgram* program = NULL;
	PocoVm* host_vm;
	uint8_t* image;
	size_t size = 0;
	int32_t result = 0;

	if (vm == NULL) {
		return;
	}
	CHECK(compile_buffer(vm, source, &program) == POCO_STATUS_OK,
		  "compiling the out-of-span fixture failed");
	image = program != NULL ? serialize(program, &size) : NULL;
	poco_program_destroy(program);
	program = NULL;
	poco_vm_destroy(vm);

	host_vm = make_vm(1);
	if (host_vm != NULL && image != NULL) {
		host_read_calls = 0;
		CHECK(poco_vm_deserialize_buffer(host_vm, image, size, &program) == POCO_STATUS_OK,
			  "loading the contracted native failed");
		CHECK(poco_vm_run(host_vm, program, NULL, &result) == POCO_STATUS_FFI_BOUNDS,
			  "an out-of-span pointer was not rejected by the contract");
		CHECK(host_read_calls == 0, "an out-of-span call entered native code");
		poco_program_destroy(program);
		program = NULL;
	}
	poco_vm_destroy(host_vm);
	free(image);

	/* The same contracted binding still accepts a call inside its span. */
	host_vm = make_vm(1);
	if (host_vm != NULL) {
		host_read_calls = 0;
		CHECK(compile_buffer(host_vm, in_span, &program) == POCO_STATUS_OK,
			  "compiling the in-span fixture failed");
		CHECK(poco_vm_run(host_vm, program, NULL, &result) == POCO_STATUS_OK,
			  "an in-span contracted call was rejected");
		CHECK(result == 7 && host_read_calls == 1,
			  "an in-span contracted call did not reach native code");
		poco_program_destroy(program);
	}
	poco_vm_destroy(host_vm);
}

/*
 * A declaration the host also supplies through a registered library is one
 * frame, bound at compile time exactly as it is without the declaration.
 */
static void test_registered_library_still_wins(void)
{
	PocoVm* vm = make_vm(1);
	PocoProgram* program = NULL;
	const Func_frame* frame;
	int32_t result = 0;
	int count = 0;

	if (vm == NULL) {
		return;
	}
	host_add_calls = 0;
	CHECK(compile_buffer(vm, native_source, &program) == POCO_STATUS_OK,
		  "compiling a declaration a registered library also supplies failed");
	if (program != NULL) {
		frame = find_frame(program, "host_add", &count);
		CHECK(count == 1, "a declared and registered native produced more than one frame");
		CHECK(frame != NULL && frame->type == CFF_C && frame->code_pt == (Code*)host_add,
			  "a declared and registered native was not bound at compile time");
		CHECK(frame != NULL && !frame->host_provided,
			  "a registered native stayed unresolved after its declaration");
		CHECK(poco_vm_run(vm, program, NULL, &result) == POCO_STATUS_OK,
			  "running a declared and registered native failed");
		CHECK(result == 42 && host_add_calls == 1,
			  "a declared and registered native did not reach the C function");
	}
	poco_program_destroy(program);
	poco_vm_destroy(vm);
}

/* An unresolved native is not callable in the process that compiled it. */
static void test_unresolved_native_is_not_callable(void)
{
	PocoVm* vm = make_vm(0);
	PocoProgram* program = NULL;
	int32_t result = 0;

	if (vm == NULL) {
		return;
	}
	host_add_calls = 0;
	CHECK(compile_buffer(vm, native_source, &program) == POCO_STATUS_OK,
		  "compiling without the host failed");
	if (program != NULL) {
		CHECK(poco_vm_run(vm, program, NULL, &result) != POCO_STATUS_OK,
			  "calling an unresolved host-provided native succeeded");
		CHECK(host_add_calls == 0, "an unresolved native reached native code");
	}
	poco_program_destroy(program);
	poco_vm_destroy(vm);
}

/*
 * The shared-header case: every unit that includes the header declares the
 * same native, and the program still resolves to one dispatch entry.
 */
static void test_shared_header_across_units(void)
{
	PocoVm* vm = make_vm(0);
	PocoProgram* program = NULL;
	uint8_t* image = NULL;
	size_t size = 0;
	int32_t result = 0;

	if (vm == NULL) {
		return;
	}
	if (poco_vm_compile_file(vm, POCO_HOST_NATIVE_USE_ROOT, &program) != POCO_STATUS_OK) {
		CHECK(0, "compiling a shared native header across units failed");
		fprintf(stderr, "poco_host_native: %s\n", poco_get_last_error(vm));
	} else {
		image = serialize(program, &size);
	}
	poco_program_destroy(program);
	program = NULL;
	poco_vm_destroy(vm);

	vm = make_vm(1);
	if (vm != NULL && image != NULL) {
		host_add_calls = 0;
		CHECK(poco_vm_deserialize_buffer(vm, image, size, &program) == POCO_STATUS_OK,
			  "loading a shared native header across units failed");
		CHECK(poco_vm_run(vm, program, NULL, &result) == POCO_STATUS_OK,
			  "running a shared native header across units failed");
		CHECK(result == 42 && host_add_calls == 2,
			  "both units' calls did not reach the one registered function");
		poco_program_destroy(program);
	}
	poco_vm_destroy(vm);
	free(image);
}

/* The region has to be well formed. */
static void test_malformed_regions_are_diagnosed(void)
{
	static const char* const sources[] = {
		"#pragma poco native end\n"
		"main() { return 0; }\n",
		"#pragma poco native begin\n"
		"#pragma poco native begin\n"
		"int host_add(int a, int b);\n"
		"#pragma poco native end\n"
		"main() { return 0; }\n",
		"#pragma poco native begin\n"
		"int host_add(int a, int b);\n"
		"main() { return 0; }\n",
		"#pragma poco native sideways\n"
		"main() { return 0; }\n",
	};
	size_t index;

	for (index = 0; index < sizeof(sources) / sizeof(sources[0]); ++index) {
		PocoVm* vm = make_vm(0);
		PocoProgram* program = NULL;

		if (vm == NULL) {
			continue;
		}
		CHECK(compile_buffer(vm, sources[index], &program) != POCO_STATUS_OK,
			  "a malformed native region compiled");
		poco_program_destroy(program);
		poco_vm_destroy(vm);
	}
}

/*
 * Every malformed spelling of the argument is refused by name, and none of them
 * takes the compiler down.  A typedef ahead of the directive is what made this
 * worth a test of its own: the fatal is raised while the tokenizer is already
 * reading the line after the typedef, so the declaration parser has to notice
 * the aborted compile instead of finishing with a symbol it never got.
 */
static void test_malformed_native_argument_is_diagnosed(void)
{
	static const char* const sources[] = {
		"#pragma poco native bogus\n"
		"main() { return 0; }\n",
		"#pragma poco native\n"
		"main() { return 0; }\n",
		"#pragma poco native begin extra\n"
		"int host_add(int a, int b);\n"
		"#pragma poco native end\n"
		"main() { return 0; }\n",
		"#pragma poco native begin\n"
		"int host_add(int a, int b);\n"
		"#pragma poco native end trailing\n"
		"main() { return 0; }\n",
		/* The crash shape: a typedef immediately before each of the above. */
		"typedef int i32;\n"
		"#pragma poco native bogus\n"
		"main() { return 0; }\n",
		"typedef int i32;\n"
		"#pragma poco native\n"
		"main() { return 0; }\n",
		"typedef int i32;\n"
		"#pragma poco native begin extra\n"
		"int host_add(int a, int b);\n"
		"#pragma poco native end\n"
		"main() { return 0; }\n",
	};
	size_t index;

	for (index = 0; index < sizeof(sources) / sizeof(sources[0]); ++index) {
		PocoVm* vm = make_vm(0);
		PocoProgram* program = NULL;
		const char* message;

		if (vm == NULL) {
			continue;
		}
		CHECK(compile_buffer(vm, sources[index], &program) != POCO_STATUS_OK,
			  "a malformed '#pragma poco native' argument compiled");
		CHECK(program == NULL, "a failed compile still produced a program");
		message = poco_get_last_error(vm);
		CHECK(message != NULL && strstr(message, "expects 'begin' or 'end'") != NULL,
			  "a malformed '#pragma poco native' argument was not reported as such");
		poco_program_destroy(program);
		poco_vm_destroy(vm);
	}
}

/* A well formed region is unaffected by the argument checking above. */
static void test_well_formed_native_argument_still_compiles(void)
{
	static const char* const sources[] = {
		"#pragma poco native begin\n"
		"int host_add(int a, int b);\n"
		"#pragma poco native end\n"
		"main() { return host_add(40, 2); }\n",
		"typedef int i32;\n"
		"#pragma poco native begin\n"
		"int host_add(int a, int b);\n"
		"#pragma poco native end\n"
		"main() { i32 sum; sum = host_add(40, 2); return sum; }\n",
		/* A ';' and a trailing comment are not trailing tokens. */
		"#pragma poco native begin ;\n"
		"int host_add(int a, int b);\n"
		"#pragma poco native end ;\n"
		"main() { return host_add(40, 2); }\n",
		"#pragma poco native begin /* open */\n"
		"int host_add(int a, int b);\n"
		"#pragma poco native end /* close */\n"
		"main() { return host_add(40, 2); }\n",
	};
	size_t index;

	for (index = 0; index < sizeof(sources) / sizeof(sources[0]); ++index) {
		PocoVm* vm = make_vm(0);
		PocoProgram* program = NULL;

		if (vm == NULL) {
			continue;
		}
		if (compile_buffer(vm, sources[index], &program) != POCO_STATUS_OK) {
			CHECK(0, "a well formed native region was refused");
			fprintf(stderr, "poco_host_native: %s\n", poco_get_last_error(vm));
		}
		poco_program_destroy(program);
		poco_vm_destroy(vm);
	}
}

int main(void)
{
	uint8_t* image;
	size_t size = 0;

	image = test_compiles_without_the_host(&size);
	test_resolves_against_a_registering_host(image, size);
	test_unprovided_native_is_refused_at_load(image, size);
	free(image);

	test_wrong_argument_count_is_a_compile_error();
	test_undeclared_function_still_fails();
	test_contract_survives_a_host_provided_declaration();
	test_registered_library_still_wins();
	test_unresolved_native_is_not_callable();
	test_shared_header_across_units();
	test_malformed_regions_are_diagnosed();
	test_malformed_native_argument_is_diagnosed();
	test_well_formed_native_argument_still_compiles();

	if (failures != 0) {
		fprintf(stderr, "poco_host_native: %d check(s) failed\n", failures);
		return 1;
	}
	return 0;
}
