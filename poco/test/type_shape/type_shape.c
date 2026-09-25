/*
 * type_shape - the declared C type behind each PocoTypeDesc.
 *
 * PocoTypeDesc::kind answers "what does a host exchange for this", so it folds
 * char, short and int into INT, float into DOUBLE and every pointer depth into
 * POPOT.  A host checking a program from an untrusted source needs the type as
 * declared: a native redeclared one pointer level deeper makes the script read
 * its own bytes as an address.  These cases pin down PocoTypeShape for every
 * base type at pointer depth 0, 1 and 2, as a Poco function, a
 * '#pragma poco native' declaration and a registered binding, plus struct
 * pointers and struct members - for a fresh compile and for the same program
 * restored from an image.
 */

#include <poco/poco.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef POCO_TYPE_SHAPE_DIR
#error "POCO_TYPE_SHAPE_DIR must name the type-shape fixture directory"
#endif

static int failures;

#define CHECK(condition, ...)                                   \
	do {                                                        \
		if (!(condition)) {                                     \
			fprintf(stderr, "poco_type_shape: " __VA_ARGS__);   \
			fputc('\n', stderr);                                \
			++failures;                                         \
		}                                                       \
	} while (0)

/* Never called: the program is only inspected.  Each binding needs its own
 * address, so each gets its own stub. */
#define STUB(n) \
	static void stub_##n(void) \
	{ \
	}
STUB(0)
STUB(1)
STUB(2)
STUB(3)
STUB(4)
STUB(5)
STUB(6)
STUB(7)
STUB(8)
STUB(9)
STUB(10)
STUB(11)
STUB(12)
STUB(13)
STUB(14)
STUB(15)
STUB(16)
STUB(17)
STUB(18)
STUB(19)
STUB(20)
STUB(21)
STUB(22)
STUB(23)
STUB(24)
STUB(25)
STUB(26)
STUB(27)
STUB(28)
STUB(29)
STUB(30)
STUB(31)
STUB(32)
STUB(33)
STUB(34)
STUB(35)
STUB(36)
STUB(37)
STUB(38)
STUB(39)
STUB(40)
STUB(41)
STUB(42)
STUB(43)
STUB(44)
STUB(45)
STUB(46)
STUB(47)
STUB(48)
STUB(49)
STUB(50)
STUB(51)
STUB(52)
STUB(53)
STUB(54)
STUB(55)
STUB(56)
STUB(57)
STUB(58)
STUB(59)
STUB(60)
STUB(61)
STUB(62)
STUB(63)
STUB(64)
STUB(65)

static const PocoBinding shape_bindings[] = {
	{"char l0_char(char a, char *b, char **c);", (PocoNativeFunction)stub_0, NULL, 0},
	{"char *l1_char(void);", (PocoNativeFunction)stub_1, NULL, 0},
	{"char **l2_char(void);", (PocoNativeFunction)stub_2, NULL, 0},
	{"unsigned char l0_uchar(unsigned char a, unsigned char *b, unsigned char **c);", (PocoNativeFunction)stub_3, NULL, 0},
	{"unsigned char *l1_uchar(void);", (PocoNativeFunction)stub_4, NULL, 0},
	{"unsigned char **l2_uchar(void);", (PocoNativeFunction)stub_5, NULL, 0},
	{"short l0_short(short a, short *b, short **c);", (PocoNativeFunction)stub_6, NULL, 0},
	{"short *l1_short(void);", (PocoNativeFunction)stub_7, NULL, 0},
	{"short **l2_short(void);", (PocoNativeFunction)stub_8, NULL, 0},
	{"unsigned short l0_ushort(unsigned short a, unsigned short *b, unsigned short **c);", (PocoNativeFunction)stub_9, NULL, 0},
	{"unsigned short *l1_ushort(void);", (PocoNativeFunction)stub_10, NULL, 0},
	{"unsigned short **l2_ushort(void);", (PocoNativeFunction)stub_11, NULL, 0},
	{"int l0_int(int a, int *b, int **c);", (PocoNativeFunction)stub_12, NULL, 0},
	{"int *l1_int(void);", (PocoNativeFunction)stub_13, NULL, 0},
	{"int **l2_int(void);", (PocoNativeFunction)stub_14, NULL, 0},
	{"unsigned int l0_uint(unsigned int a, unsigned int *b, unsigned int **c);", (PocoNativeFunction)stub_15, NULL, 0},
	{"unsigned int *l1_uint(void);", (PocoNativeFunction)stub_16, NULL, 0},
	{"unsigned int **l2_uint(void);", (PocoNativeFunction)stub_17, NULL, 0},
	{"long l0_long(long a, long *b, long **c);", (PocoNativeFunction)stub_18, NULL, 0},
	{"long *l1_long(void);", (PocoNativeFunction)stub_19, NULL, 0},
	{"long **l2_long(void);", (PocoNativeFunction)stub_20, NULL, 0},
	{"unsigned long l0_ulong(unsigned long a, unsigned long *b, unsigned long **c);", (PocoNativeFunction)stub_21, NULL, 0},
	{"unsigned long *l1_ulong(void);", (PocoNativeFunction)stub_22, NULL, 0},
	{"unsigned long **l2_ulong(void);", (PocoNativeFunction)stub_23, NULL, 0},
	{"float l0_float(float a, float *b, float **c);", (PocoNativeFunction)stub_24, NULL, 0},
	{"float *l1_float(void);", (PocoNativeFunction)stub_25, NULL, 0},
	{"float **l2_float(void);", (PocoNativeFunction)stub_26, NULL, 0},
	{"double l0_double(double a, double *b, double **c);", (PocoNativeFunction)stub_27, NULL, 0},
	{"double *l1_double(void);", (PocoNativeFunction)stub_28, NULL, 0},
	{"double **l2_double(void);", (PocoNativeFunction)stub_29, NULL, 0},
	{"void l0_void(void *b, void **c);", (PocoNativeFunction)stub_30, NULL, 0},
	{"void *l1_void(void);", (PocoNativeFunction)stub_31, NULL, 0},
	{"void **l2_void(void);", (PocoNativeFunction)stub_32, NULL, 0},
};

/*
 * A '#pragma poco native' declaration compiles to a frame with no address, so
 * a restored image needs the host to supply one.  Only the restoring VM
 * registers these: at compile time the n-family must come from the pragma
 * region alone.
 */
static const PocoBinding pragma_bindings[] = {
	{"char n0_char(char a, char *b, char **c);", (PocoNativeFunction)stub_33, NULL, 0},
	{"char *n1_char(void);", (PocoNativeFunction)stub_34, NULL, 0},
	{"char **n2_char(void);", (PocoNativeFunction)stub_35, NULL, 0},
	{"unsigned char n0_uchar(unsigned char a, unsigned char *b, unsigned char **c);", (PocoNativeFunction)stub_36, NULL, 0},
	{"unsigned char *n1_uchar(void);", (PocoNativeFunction)stub_37, NULL, 0},
	{"unsigned char **n2_uchar(void);", (PocoNativeFunction)stub_38, NULL, 0},
	{"short n0_short(short a, short *b, short **c);", (PocoNativeFunction)stub_39, NULL, 0},
	{"short *n1_short(void);", (PocoNativeFunction)stub_40, NULL, 0},
	{"short **n2_short(void);", (PocoNativeFunction)stub_41, NULL, 0},
	{"unsigned short n0_ushort(unsigned short a, unsigned short *b, unsigned short **c);", (PocoNativeFunction)stub_42, NULL, 0},
	{"unsigned short *n1_ushort(void);", (PocoNativeFunction)stub_43, NULL, 0},
	{"unsigned short **n2_ushort(void);", (PocoNativeFunction)stub_44, NULL, 0},
	{"int n0_int(int a, int *b, int **c);", (PocoNativeFunction)stub_45, NULL, 0},
	{"int *n1_int(void);", (PocoNativeFunction)stub_46, NULL, 0},
	{"int **n2_int(void);", (PocoNativeFunction)stub_47, NULL, 0},
	{"unsigned int n0_uint(unsigned int a, unsigned int *b, unsigned int **c);", (PocoNativeFunction)stub_48, NULL, 0},
	{"unsigned int *n1_uint(void);", (PocoNativeFunction)stub_49, NULL, 0},
	{"unsigned int **n2_uint(void);", (PocoNativeFunction)stub_50, NULL, 0},
	{"long n0_long(long a, long *b, long **c);", (PocoNativeFunction)stub_51, NULL, 0},
	{"long *n1_long(void);", (PocoNativeFunction)stub_52, NULL, 0},
	{"long **n2_long(void);", (PocoNativeFunction)stub_53, NULL, 0},
	{"unsigned long n0_ulong(unsigned long a, unsigned long *b, unsigned long **c);", (PocoNativeFunction)stub_54, NULL, 0},
	{"unsigned long *n1_ulong(void);", (PocoNativeFunction)stub_55, NULL, 0},
	{"unsigned long **n2_ulong(void);", (PocoNativeFunction)stub_56, NULL, 0},
	{"float n0_float(float a, float *b, float **c);", (PocoNativeFunction)stub_57, NULL, 0},
	{"float *n1_float(void);", (PocoNativeFunction)stub_58, NULL, 0},
	{"float **n2_float(void);", (PocoNativeFunction)stub_59, NULL, 0},
	{"double n0_double(double a, double *b, double **c);", (PocoNativeFunction)stub_60, NULL, 0},
	{"double *n1_double(void);", (PocoNativeFunction)stub_61, NULL, 0},
	{"double **n2_double(void);", (PocoNativeFunction)stub_62, NULL, 0},
	{"void n0_void(void *b, void **c);", (PocoNativeFunction)stub_63, NULL, 0},
	{"void *n1_void(void);", (PocoNativeFunction)stub_64, NULL, 0},
	{"void **n2_void(void);", (PocoNativeFunction)stub_65, NULL, 0},
};

static const PocoLibrary pragma_library = {
	"type-shape-pragma", pragma_bindings, sizeof(pragma_bindings) / sizeof(pragma_bindings[0]),
	NULL,                NULL,            NULL,
};

static const PocoLibrary shape_library = {
	"type-shape-test", shape_bindings, sizeof(shape_bindings) / sizeof(shape_bindings[0]),
	NULL,              NULL,           NULL,
};

typedef struct BaseCase {
	const char* suffix;
	uint32_t base_type;
} BaseCase;

static const BaseCase bases[] = {
	{"char", POCO_BASE_TYPE_CHAR},
	{"uchar", POCO_BASE_TYPE_UCHAR},
	{"short", POCO_BASE_TYPE_SHORT},
	{"ushort", POCO_BASE_TYPE_USHORT},
	{"int", POCO_BASE_TYPE_INT},
	{"uint", POCO_BASE_TYPE_UINT},
	{"long", POCO_BASE_TYPE_LONG},
	{"ulong", POCO_BASE_TYPE_ULONG},
	{"float", POCO_BASE_TYPE_FLOAT},
	{"double", POCO_BASE_TYPE_DOUBLE},
	{"void", POCO_BASE_TYPE_VOID},
};

#define BASE_COUNT (sizeof(bases) / sizeof(bases[0]))

static void expect_shape(const PocoTypeShape* shape, uint32_t base, uint32_t depth,
						 uint32_t array_rank, const char* what)
{
	CHECK(shape->base_type == base, "%s: base_type %u, expected %u", what,
		  (unsigned)shape->base_type, (unsigned)base);
	CHECK(shape->pointer_depth == depth, "%s: pointer_depth %u, expected %u", what,
		  (unsigned)shape->pointer_depth, (unsigned)depth);
	CHECK(shape->array_rank == array_rank, "%s: array_rank %u, expected %u", what,
		  (unsigned)shape->array_rank, (unsigned)array_rank);
	CHECK(shape->is_function == 0, "%s: is_function set", what);
}

static void expect_return(PocoActivation* activation, const char* function, uint32_t base,
						  uint32_t depth, const char* label)
{
	PocoTypeShape shape;
	char what[160];

	snprintf(what, sizeof(what), "%s: %s return", label, function);
	if (poco_activation_function_return_shape(activation, function, &shape) != POCO_STATUS_OK) {
		CHECK(0, "%s is unreadable", what);
		return;
	}
	expect_shape(&shape, base, depth, 0, what);
}

static void expect_parameter(PocoActivation* activation, const char* function, size_t index,
							 uint32_t base, uint32_t depth, const char* label)
{
	PocoTypeShape shape;
	char what[160];

	snprintf(what, sizeof(what), "%s: %s parameter %zu", label, function, index);
	if (poco_activation_function_parameter_shape(activation, function, index, &shape) !=
		POCO_STATUS_OK) {
		CHECK(0, "%s is unreadable", what);
		return;
	}
	expect_shape(&shape, base, depth, 0, what);
}

/* f = Poco function, n = '#pragma poco native', l = registered binding. */
static void check_families(PocoActivation* activation, const char* label)
{
	static const char families[] = {'f', 'n', 'l'};
	size_t family;
	size_t base;

	for (family = 0; family < sizeof(families); ++family) {
		for (base = 0; base < BASE_COUNT; ++base) {
			const BaseCase* entry = &bases[base];
			char name[64];
			uint32_t depth;
			uint32_t first_depth;

			for (depth = 0; depth < 3; ++depth) {
				snprintf(name, sizeof(name), "%c%u_%s", families[family], (unsigned)depth,
						 entry->suffix);
				expect_return(activation, name, entry->base_type, depth, label);
			}
			snprintf(name, sizeof(name), "%c0_%s", families[family], entry->suffix);
			first_depth = entry->base_type == POCO_BASE_TYPE_VOID ? 1u : 0u;
			for (depth = first_depth; depth < 3; ++depth) {
				expect_parameter(activation, name, depth - first_depth, entry->base_type, depth,
								 label);
			}
		}
	}
}

typedef struct MemberCase {
	uint32_t base_type;
	uint32_t pointer_depth;
	uint32_t array_rank;
} MemberCase;

static const MemberCase probe_members[] = {
	{POCO_BASE_TYPE_CHAR, 0, 0},   {POCO_BASE_TYPE_UCHAR, 0, 0},  {POCO_BASE_TYPE_SHORT, 0, 0},
	{POCO_BASE_TYPE_USHORT, 0, 0}, {POCO_BASE_TYPE_INT, 0, 0},    {POCO_BASE_TYPE_UINT, 0, 0},
	{POCO_BASE_TYPE_LONG, 0, 0},   {POCO_BASE_TYPE_ULONG, 0, 0},  {POCO_BASE_TYPE_FLOAT, 0, 0},
	{POCO_BASE_TYPE_DOUBLE, 0, 0}, {POCO_BASE_TYPE_CHAR, 1, 0},   {POCO_BASE_TYPE_FLOAT, 2, 0},
	{POCO_BASE_TYPE_VOID, 1, 0},   {POCO_BASE_TYPE_STRUCT, 1, 0}, {POCO_BASE_TYPE_INT, 0, 1},
};

#define PROBE_MEMBER_COUNT (sizeof(probe_members) / sizeof(probe_members[0]))

static void check_structs(PocoActivation* activation, PocoProgram* program, const char* label)
{
	PocoTypeDesc desc;
	PocoTypeShape shape;
	size_t index;
	char what[160];

	expect_return(activation, "t_link", POCO_BASE_TYPE_USHORT, 0, label);
	expect_parameter(activation, "t_link", 0, POCO_BASE_TYPE_USHORT, 0, label);
	expect_parameter(activation, "t_link", 1, POCO_BASE_TYPE_USHORT, 1, label);
	expect_parameter(activation, "t_link", 2, POCO_BASE_TYPE_USHORT, 2, label);

	expect_return(activation, "p_link", POCO_BASE_TYPE_STRUCT, 1, label);
	expect_parameter(activation, "p_link", 0, POCO_BASE_TYPE_STRUCT, 1, label);
	expect_parameter(activation, "p_link", 1, POCO_BASE_TYPE_STRUCT, 2, label);

	if (poco_activation_function_parameter(activation, "p_link", 1, NULL, &desc) !=
			POCO_STATUS_OK ||
		!desc.is_struct) {
		CHECK(0, "%s: p_link's struct parameter does not name its struct", label);
		return;
	}
	CHECK(desc.kind == POCO_CALLBACK_VALUE_POPOT, "%s: struct probe ** changed kind", label);
	CHECK(desc.struct_member_count == PROBE_MEMBER_COUNT, "%s: struct probe has %zu members",
		  label, desc.struct_member_count);
	for (index = 0; index < PROBE_MEMBER_COUNT; ++index) {
		snprintf(what, sizeof(what), "%s: struct probe member %zu", label, index);
		if (poco_program_struct_member_shape(program, desc.struct_id, index, &shape) !=
			POCO_STATUS_OK) {
			CHECK(0, "%s is unreadable", what);
			continue;
		}
		expect_shape(&shape, probe_members[index].base_type, probe_members[index].pointer_depth,
					 probe_members[index].array_rank, what);
	}
	CHECK(poco_program_struct_member_shape(program, desc.struct_id, PROBE_MEMBER_COUNT, &shape) ==
			  POCO_STATUS_PARAMETER_RANGE,
		  "%s: a member index past the last is not out of range", label);
}

static void check_errors(PocoActivation* activation, PocoProgram* program)
{
	PocoTypeShape shape;
	PocoTypeShape untouched;

	memset(&shape, 0xA5, sizeof(shape));
	memset(&untouched, 0xA5, sizeof(untouched));
	CHECK(poco_activation_function_return_shape(activation, "no_such_function", &shape) ==
			  POCO_STATUS_NOT_FOUND,
		  "an absent function's return shape is not reported as missing");
	CHECK(poco_activation_function_parameter_shape(activation, "no_such_function", 0, &shape) ==
			  POCO_STATUS_NOT_FOUND,
		  "an absent function's parameter shape is not reported as missing");
	CHECK(poco_activation_function_parameter_shape(activation, "f0_int", 3, &shape) ==
			  POCO_STATUS_PARAMETER_RANGE,
		  "a parameter index past the last is not out of range");
	CHECK(poco_program_struct_member_shape(program, POCO_STRUCT_NONE, 0, &shape) ==
			  POCO_STATUS_PARAMETER_RANGE,
		  "POCO_STRUCT_NONE names a struct");
	CHECK(memcmp(&shape, &untouched, sizeof(shape)) == 0, "a failed lookup wrote its output");
	CHECK(poco_activation_function_return_shape(activation, "f0_int", NULL) ==
			  POCO_STATUS_NULL_REFERENCE,
		  "a NULL output is accepted");
	CHECK(poco_activation_function_parameter_shape(NULL, "f0_int", 0, &shape) ==
			  POCO_STATUS_NULL_REFERENCE,
		  "a NULL activation is accepted");
	CHECK(poco_program_struct_member_shape(NULL, 0, 0, &shape) == POCO_STATUS_NULL_REFERENCE,
		  "a NULL program is accepted");
}

static void check_program(PocoProgram* program, const char* label)
{
	PocoActivation* activation = NULL;

	if (poco_activation_acquire(program, &activation) != POCO_STATUS_OK || activation == NULL) {
		CHECK(0, "%s: acquiring an activation failed", label);
		return;
	}
	check_families(activation, label);
	check_structs(activation, program, label);
	check_errors(activation, program);
	poco_activation_release(activation);
}

static PocoVm* make_vm(int restoring)
{
	PocoVm* vm = NULL;

	if (poco_vm_create(NULL, &vm) != POCO_STATUS_OK) {
		CHECK(0, "VM creation failed");
		return NULL;
	}
	if (poco_vm_register_library(vm, &shape_library) != POCO_STATUS_OK) {
		CHECK(0, "binding registration failed: %s", poco_get_last_error(vm));
	}
	if (restoring && poco_vm_register_library(vm, &pragma_library) != POCO_STATUS_OK) {
		CHECK(0, "pragma binding registration failed: %s", poco_get_last_error(vm));
	}
	return vm;
}

static PocoProgram* restore(PocoVm* vm, PocoProgram* program)
{
	PocoProgram* restored = NULL;
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
		CHECK(0, "image allocation failed");
		return NULL;
	}
	if (poco_program_serialize_buffer(program, POCO_DEBUG_LEVEL_MINIMAL, image, size, &size) !=
		POCO_STATUS_OK) {
		CHECK(0, "serialization failed");
	} else {
		PocoStatus status = poco_vm_deserialize_buffer(vm, image, size, &restored);

		if (status != POCO_STATUS_OK) {
			CHECK(0, "deserialization failed (%d): %s", (int)status, poco_get_last_error(vm));
			restored = NULL;
		}
	}
	free(image);
	return restored;
}

int main(void)
{
	const char* names[1] = {POCO_TYPE_SHAPE_DIR "shapes.poc"};
	PocoVm* vm = make_vm(0);
	PocoVm* restoring_vm;
	PocoProgram* program = NULL;
	PocoProgram* restored;

	if (vm == NULL) {
		return 1;
	}
	if (poco_vm_compile_files(vm, names, 1, &program) != POCO_STATUS_OK) {
		fprintf(stderr, "poco_type_shape: compile failed: %s\n", poco_get_last_error(vm));
		poco_vm_destroy(vm);
		return 1;
	}
	check_program(program, "fresh");

	/* A separate VM, so nothing the compile left behind can answer for the image. */
	restoring_vm = make_vm(1);
	if (restoring_vm != NULL) {
		restored = restore(restoring_vm, program);
		if (restored != NULL) {
			check_program(restored, "restored");
			poco_program_destroy(restored);
		}
		poco_vm_destroy(restoring_vm);
	}
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	if (failures != 0) {
		fprintf(stderr, "poco_type_shape: %d failure(s)\n", failures);
	}
	return failures == 0 ? 0 : 1;
}
