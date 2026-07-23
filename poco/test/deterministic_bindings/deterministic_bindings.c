#include <poco/poco.h>

#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

static const char graph_source[] =
	"main()\n"
	"{\n"
	"  int result;\n"
	"  result = 0;\n"
	"  if (acos(1.0) != 0.0 || asin(0.0) != 0.0 || atan(0.0) != 0.0) result |= 1;\n"
	"  if (atan2(0.0, 1.0) != 0.0 || ceil(1.25) != 2.0) result |= 2;\n"
	"  if (cos(0.0) != 1.0 || cosh(0.0) != 1.0 || exp(0.0) != 1.0) result |= 4;\n"
	"  if (fabs(-2.0) != 2.0 || floor(1.75) != 1.0 || fmod(5.0, 2.0) != 1.0) result |= 8;\n"
	"  if (log(1.0) != 0.0 || log10(1.0) != 0.0 || pow(2.0, 3.0) != 8.0) result |= 16;\n"
	"  if (sin(0.0) != 0.0 || sinh(0.0) != 0.0 || sqrt(9.0) != 3.0) result |= 32;\n"
	"  if (tan(0.0) != 0.0 || tanh(0.0) != 0.0) result |= 64;\n"
	"  if ((6 & 3) != 2 || (6 | 3) != 7 || (6 ^ 3) != 5) result |= 128;\n"
	"  if ((1 << 4) != 16 || (16 >> 2) != 4 || !1 || ~0 != -1) result |= 256;\n"
	"  return result;\n"
	"}\n";

static int check(int condition, const char *message, const char *locale_name)
{
	if (condition)
		return 1;
	fprintf(stderr, "poco_deterministic_bindings: %s (locale=%s)\n",
		message, locale_name);
	return 0;
}

static int compile_rejected(PocoVm *vm, const char *source, const char *locale_name)
{
	PocoProgram *program = NULL;
	PocoStatus status = poco_vm_compile_buffer(vm, "forbidden-ambient-input",
		source, strlen(source), &program);

	if (program != NULL)
		poco_program_destroy(program);
	return check(status == POCO_STATUS_REPORTED && program == NULL,
		"trusted graph table exposed locale/time/random input", locale_name);
}

static int run_locale(const char *locale_name)
{
	static const char *const rejected_sources[] = {
		"main() { return time(0); }\n",
		"main() { return clock(); }\n",
		"main() { return rand(); }\n",
		"main() { return random(); }\n",
		"main() { return atof(\"1.5\") == 1.5; }\n",
	};
	PocoVm *vm = NULL;
	PocoProgram *program = NULL;
	PocoStatus status;
	int32_t result = -1;
	size_t index;
	int ok = 1;

	if (setlocale(LC_ALL, locale_name) == NULL)
		return 1; /* The target does not install this optional locale. */

	status = poco_vm_create(NULL, &vm);
	ok &= check(status == POCO_STATUS_OK && vm != NULL, "create VM", locale_name);
	if (vm == NULL)
		return 0;
	ok &= check(poco_vm_register_trusted_graph_library(vm) == POCO_STATUS_OK,
		"register trusted graph table", locale_name);
	status = poco_vm_compile_buffer(vm, "deterministic-graph", graph_source,
		sizeof(graph_source) - 1, &program);
	ok &= check(status == POCO_STATUS_OK && program != NULL,
		"compile graph source", locale_name);

	if (program != NULL) {
		int32_t first_result = -1;

		/*
		 * The source is compiled under this locale, so a locale-sensitive
		 * numeric parse (e.g. a comma decimal point) or a locale-sensitive math
		 * binding would flip an identity and move the result off zero. A couple
		 * of re-runs additionally confirm the second run reproduces the first
		 * byte-for-byte, i.e. no ambient state leaks between runs.
		 */
		for (index = 0; index < 3; ++index) {
			result = -1;
			status = poco_vm_run(vm, program, NULL, &result);
			ok &= check(status == POCO_STATUS_OK && result == 0,
				"graph output changed", locale_name);
			if (index == 0)
				first_result = result;
			else
				ok &= check(result == first_result,
					"graph output was not reproducible across runs", locale_name);
		}
	}

	for (index = 0; index < ARRAY_COUNT(rejected_sources); ++index)
		ok &= compile_rejected(vm, rejected_sources[index], locale_name);

	if (program != NULL)
		poco_program_destroy(program);
	poco_vm_destroy(vm);
	return ok;
}

int main(void)
{
	static const char *const locales[] = {
		"C",
		"en_US.UTF-8",
		"fr_FR.UTF-8",
		"de_DE.UTF-8",
		"fr_CA.UTF-8",
	};
	char saved_locale[128];
	const char *current_locale = setlocale(LC_ALL, NULL);
	size_t index;
	int ok = 1;

	if (current_locale == NULL || strlen(current_locale) >= sizeof(saved_locale))
		return 1;
	memcpy(saved_locale, current_locale, strlen(current_locale) + 1);

	for (index = 0; index < ARRAY_COUNT(locales); ++index)
		ok &= run_locale(locales[index]);

	if (setlocale(LC_ALL, saved_locale) == NULL)
		ok = 0;
	return ok ? 0 : 1;
}
