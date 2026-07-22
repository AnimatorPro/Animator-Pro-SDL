#include <poco/poco.h>

#include <stdio.h>
#include <string.h>

typedef struct RejectedSource
{
	const char *category;
	const char *source;
} RejectedSource;

static int check(int condition, const char *message)
{
	if (condition)
		return 1;
	fprintf(stderr, "poco_trusted_graph_table: %s\n", message);
	return 0;
}

static int compile_rejected(PocoVm *vm, const RejectedSource *fixture)
{
	PocoProgram *program = NULL;
	PocoStatus status = poco_vm_compile_buffer(vm, fixture->category,
		fixture->source, strlen(fixture->source), &program);

	if (program != NULL)
		poco_program_destroy(program);
	if (status == POCO_STATUS_REPORTED && program == NULL)
		return 1;
	fprintf(stderr, "poco_trusted_graph_table: %s binding unexpectedly exposed"
		" (status=%d)\n", fixture->category, (int)status);
	return 0;
}

int main(void)
{
	static const char graph_source[] =
		"main()\n"
		"{\n"
		"  if (acos(1.0) != 0.0 || asin(0.0) != 0.0 || atan(0.0) != 0.0) return 1;\n"
		"  if (atan2(0.0, 1.0) != 0.0 || ceil(1.25) != 2.0) return 2;\n"
		"  if (cos(0.0) != 1.0 || cosh(0.0) != 1.0 || exp(0.0) != 1.0) return 3;\n"
		"  if (fabs(-2.0) != 2.0 || floor(1.75) != 1.0 || fmod(5.0, 2.0) != 1.0) return 4;\n"
		"  if (log(1.0) != 0.0 || log10(1.0) != 0.0 || pow(2.0, 3.0) != 8.0) return 5;\n"
		"  if (sin(0.0) != 0.0 || sinh(0.0) != 0.0 || sqrt(9.0) != 3.0) return 6;\n"
		"  if (tan(0.0) != 0.0 || tanh(0.0) != 0.0) return 7;\n"
		"  if ((6 & 3) != 2 || (6 | 3) != 7 || (6 ^ 3) != 5) return 8;\n"
		"  if ((1 << 4) != 16 || (16 >> 2) != 4 || !1 || ~0 != -1) return 9;\n"
		"  if (!(1 && 2) || (0 || 0)) return 10;\n"
		"  return 0;\n"
		"}\n";
	static const RejectedSource rejected[] = {
		{"console", "main() { return puts(\"x\"); }\n"},
		{"string", "main() { return strlen(\"x\"); }\n"},
		{"memory", "main() { return malloc(4) == 0; }\n"},
		{"file", "main() { return fopen(\"x\", \"r\") == 0; }\n"},
		{"path", "main() { char a[8]; return fnsplit(\"x\", a, a, a, a); }\n"},
		{"time", "main() { return time(0); }\n"},
	};
	PocoVm *empty_vm = NULL;
	PocoVm *graph_vm = NULL;
	PocoProgram *program = NULL;
	PocoStatus status;
	int32_t result = -1;
	size_t index;
	int ok = 1;

	ok &= check(poco_vm_create(NULL, &empty_vm) == POCO_STATUS_OK,
		"create empty VM");
	if (!ok)
		return 1;
	status = poco_vm_compile_buffer(empty_vm, "unregistered-math", graph_source,
		sizeof(graph_source) - 1, &program);
	ok &= check(status == POCO_STATUS_REPORTED && program == NULL,
		"empty VM unexpectedly exposed graph math");
	poco_vm_destroy(empty_vm);

	ok &= check(poco_vm_create(NULL, &graph_vm) == POCO_STATUS_OK,
		"create graph VM");
	ok &= check(poco_vm_register_trusted_graph_library(graph_vm) == POCO_STATUS_OK,
		"register trusted graph table");
	ok &= check(poco_vm_register_trusted_graph_library(graph_vm) == POCO_STATUS_OK,
		"trusted graph registration is not idempotent");
	if (!ok) {
		poco_vm_destroy(graph_vm);
		return 1;
	}

	status = poco_vm_compile_buffer(graph_vm, "trusted-graph", graph_source,
		sizeof(graph_source) - 1, &program);
	ok &= check(status == POCO_STATUS_OK && program != NULL,
		"trusted graph math/operator fixture did not compile");
	if (program != NULL) {
		status = poco_vm_run(graph_vm, program, NULL, &result);
		ok &= check(status == POCO_STATUS_OK && result == 0,
			"trusted graph math/operator fixture failed");
		poco_program_destroy(program);
		program = NULL;
	}

	for (index = 0; index < sizeof(rejected) / sizeof(rejected[0]); ++index)
		ok &= compile_rejected(graph_vm, &rejected[index]);

	poco_vm_destroy(graph_vm);
	return ok ? 0 : 1;
}
