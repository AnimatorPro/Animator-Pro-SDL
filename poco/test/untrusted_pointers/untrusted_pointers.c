/*
 * Untrusted-pointer hardening: what a script may and may not reach through
 * pointers it holds.  Reads of its own memory and of its string literals are
 * allowed; writes into literals, and reads that run off the end of one, are
 * refused with POCO_STATUS_FFI_BOUNDS.  A literal holding "\0" keeps its full
 * length, through serialization too.  Struct copies check the destination for
 * write access and the source for read access.
 */
#include <poco/poco.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char borrowed_bytes[4] = {3, 4, 5, 6};
static int failures;

#define CHECK(condition, message)                                 \
	do {                                                          \
		if (!(condition)) {                                       \
			fprintf(stderr, "poco_untrusted_pointers: %s\n", message); \
			++failures;                                           \
		}                                                         \
	} while (0)

static char* borrowed_span(void)
{
	return (char*)borrowed_bytes;
}

static const PocoBindingContract borrowed_contract = {
	.return_value =
		{
			.origin = POCO_POINTER_RETURN_BORROWED,
			.permissions = POCO_POINTER_PERMISSION_READ,
		},
};

static const PocoBinding bindings[] = {
	{"char *borrowed_span();", (PocoNativeFunction)borrowed_span, &borrowed_contract},
};

static const PocoLibrary library = {
	"untrusted-pointers", bindings, sizeof(bindings) / sizeof(bindings[0]), NULL, NULL, NULL,
};

static PocoVm* make_vm(int untrusted)
{
	PocoVm* vm = NULL;
	PocoVmOptions options = {0};

	CHECK(poco_vm_create(&options, &vm) == POCO_STATUS_OK, "create VM");
	if (vm == NULL) {
		return NULL;
	}
	CHECK(poco_vm_register_standard_library(vm) == POCO_STATUS_OK, "register standard library");
	CHECK(poco_vm_register_library(vm, &library) == POCO_STATUS_OK, "register test bindings");
	CHECK(poco_vm_register_borrowed_span(vm, borrowed_bytes, sizeof(borrowed_bytes),
										 POCO_POINTER_PERMISSION_READ) == POCO_STATUS_OK,
		  "register read-only borrowed span");
	CHECK(poco_vm_set_untrusted_pointers(vm, untrusted) == POCO_STATUS_OK,
		  "set untrusted pointers");
	return vm;
}

static PocoStatus run_program(PocoVm* vm, PocoProgram* program, const char* trace_file,
							  int32_t* out_result)
{
	PocoRunOptions options = {0};
	int32_t result = -1;
	PocoStatus status;

	options.trace_file = trace_file;
	status = poco_vm_run(vm, program, &options, &result);
	if (out_result != NULL) {
		*out_result = result;
	}
	return status;
}

static PocoStatus run_fixture(int untrusted, const char* name, int32_t* out_result)
{
	PocoVm* vm = make_vm(untrusted);
	PocoProgram* program = NULL;
	PocoStatus status;
	char path[512];

	if (vm == NULL) {
		return POCO_STATUS_INTERNAL_ERROR;
	}
	snprintf(path, sizeof(path), "%s/%s", POCO_UNTRUSTED_POINTERS_FIXTURE_DIR, name);
	status = poco_vm_compile_file(vm, path, &program);
	if (status == POCO_STATUS_OK) {
		status = run_program(vm, program, POCO_UNTRUSTED_POINTERS_TRACE_FILE, out_result);
	} else {
		fprintf(stderr, "poco_untrusted_pointers: compiling %s: %s\n", name,
				poco_get_last_error(vm));
	}
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return status;
}

static void test_scripts_read_literals(void)
{
	int32_t result = -1;

	CHECK(run_fixture(0, "literal_reads.poc", &result) == POCO_STATUS_OK && result == 0,
		  "literal reads without hardening");
	result = -1;
	CHECK(run_fixture(1, "literal_reads.poc", &result) == POCO_STATUS_OK && result == 0,
		  "literal reads with hardening");
	result = -1;
	CHECK(run_fixture(1, "own_memory.poc", &result) == POCO_STATUS_OK && result == 0,
		  "global and stack arrays with hardening");
	result = -1;
	CHECK(run_fixture(0, "literal_embedded_nul.poc", &result) == POCO_STATUS_OK && result == 0,
		  "literal with an embedded NUL without hardening");
	result = -1;
	CHECK(run_fixture(1, "literal_embedded_nul.poc", &result) == POCO_STATUS_OK && result == 0,
		  "literal with an embedded NUL with hardening");
}

static void test_literals_stay_read_only(void)
{
	CHECK(run_fixture(1, "literal_write.poc", NULL) == POCO_STATUS_FFI_BOUNDS,
		  "a store through a literal pointer is refused");
	CHECK(run_fixture(1, "literal_copy_into.poc", NULL) == POCO_STATUS_FFI_BOUNDS,
		  "a struct copy into a literal is refused");
	CHECK(run_fixture(1, "literal_straddle.poc", NULL) == POCO_STATUS_FFI_BOUNDS,
		  "a read running past the end of a literal is refused");
}

/* A deserialized program allocates its literals afresh; they must be admitted too. */
static void test_deserialized_literals(const char* name)
{
	PocoVm* vm = make_vm(1);
	PocoProgram* compiled = NULL;
	PocoProgram* loaded = NULL;
	unsigned char* bytes = NULL;
	size_t size = 0;
	int32_t result = -1;
	char path[512];

	if (vm == NULL) {
		return;
	}
	snprintf(path, sizeof(path), "%s/%s", POCO_UNTRUSTED_POINTERS_FIXTURE_DIR, name);
	CHECK(poco_vm_compile_file(vm, path, &compiled) == POCO_STATUS_OK,
		  "compile fixture for serialization");
	if (compiled != NULL) {
		CHECK(poco_program_serialize_buffer(compiled, POCO_DEBUG_LEVEL_MINIMAL, NULL, 0, &size) ==
				  POCO_STATUS_BUFFER_TOO_SMALL,
			  "measure serialized image");
		bytes = malloc(size);
		CHECK(bytes != NULL, "allocate serialized image");
	}
	if (bytes != NULL) {
		CHECK(poco_program_serialize_buffer(compiled, POCO_DEBUG_LEVEL_MINIMAL, bytes, size,
											&size) == POCO_STATUS_OK,
			  "serialize fixture");
		CHECK(poco_vm_deserialize_buffer(vm, bytes, size, &loaded) == POCO_STATUS_OK,
			  "deserialize fixture");
	}
	if (loaded != NULL) {
		CHECK(run_program(vm, loaded, POCO_UNTRUSTED_POINTERS_TRACE_FILE, &result) ==
					  POCO_STATUS_OK &&
				  result == 0,
			  "deserialized program reads its literals with hardening");
	}
	poco_program_destroy(loaded);
	poco_program_destroy(compiled);
	free(bytes);
	poco_vm_destroy(vm);
}

static void test_refusal_trace(void)
{
	char text[1024];
	size_t length;
	FILE* file;

	remove(POCO_UNTRUSTED_POINTERS_TRACE_FILE);
	CHECK(run_fixture(1, "literal_write.poc", NULL) == POCO_STATUS_FFI_BOUNDS,
		  "literal write for the trace");
	file = fopen(POCO_UNTRUSTED_POINTERS_TRACE_FILE, "r");
	CHECK(file != NULL, "refusal wrote a trace");
	if (file == NULL) {
		return;
	}
	length = fread(text, 1, sizeof(text) - 1, file);
	text[length] = '\0';
	fclose(file);
	CHECK(strstr(text, "outside VM memory (untrusted-pointer check)") != NULL,
		  "trace names the untrusted-pointer check");
	CHECK(strstr(text, "library function") == NULL,
		  "trace does not blame a library function");
	CHECK(strstr(text, "near line") != NULL, "trace names a line");
}

static void test_struct_copy_permissions(void)
{
	int32_t result = -1;
	int untrusted;

	for (untrusted = 0; untrusted <= 1; ++untrusted) {
		result = -1;
		CHECK(run_fixture(untrusted, "borrowed_copy_from.poc", &result) == POCO_STATUS_OK &&
				  result == 9,
			  "a struct copy may read a read-only borrowed span");
		CHECK(run_fixture(untrusted, "borrowed_copy_into.poc", NULL) == POCO_STATUS_FFI_BOUNDS,
			  "a struct copy may not write a read-only borrowed span");
		CHECK(borrowed_bytes[0] == 3 && borrowed_bytes[3] == 6,
			  "read-only borrowed span is unchanged");
	}
}

int main(void)
{
	test_scripts_read_literals();
	test_literals_stay_read_only();
	test_deserialized_literals("literal_reads.poc");
	test_deserialized_literals("literal_embedded_nul.poc");
	test_refusal_trace();
	test_struct_copy_permissions();
	remove(POCO_UNTRUSTED_POINTERS_TRACE_FILE);
	if (failures != 0) {
		fprintf(stderr, "poco_untrusted_pointers: %d failure(s)\n", failures);
		return 1;
	}
	return 0;
}
