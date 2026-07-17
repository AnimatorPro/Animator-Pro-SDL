#include <poco/poco.h>

#include <stddef.h>
#include <stdio.h>

#define FIXTURE_PATH(name) POCO_PORT_RUNTIME_FIXTURE_DIR "/" name

/*
 * Animator exports this legacy helper family.  Poco must never bind its
 * internal runtime to it: the host deliberately rejects every allocation so
 * an accidental unprefixed Poco reference fails deterministically.
 */
static int host_allocator_calls;

void *pj_malloc(size_t size)
{
	(void)size;
	++host_allocator_calls;
	return NULL;
}

void *pj_zalloc(size_t size)
{
	(void)size;
	++host_allocator_calls;
	return NULL;
}

void pj_free(void *pointer)
{
	(void)pointer;
	++host_allocator_calls;
}

void pj_gentle_free(void *pointer)
{
	(void)pointer;
	++host_allocator_calls;
}

void pj_freez(void *pointer)
{
	(void)pointer;
	++host_allocator_calls;
}

int pj_delete(const char *name)
{
	(void)name;
	return -1;
}

int pj_ioerr(void)
{
	return -1;
}

void upc(char *text)
{
	(void)text;
}

char *clone_string(const char *text)
{
	(void)text;
	return NULL;
}

void init_stdfiles(void) {}
void cleanup_lfiles(void) {}

static int check(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "poco_port_runtime: %s\n", message);
	}
	return condition;
}

int main(void)
{
	PocoVm *vm = NULL;
	PocoProgram *program = NULL;
	PocoRunOptions run_options = {NULL, NULL, NULL};
	int32_t result = 0;
	int ok = 1;

	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "create VM");
	if (ok) {
		ok &= check(poco_vm_register_standard_library(vm) == POCO_STATUS_OK,
			"register standard library");
	}
	if (ok) {
		ok &= check(poco_vm_compile_file(vm, FIXTURE_PATH("run.poc"), &program) ==
				POCO_STATUS_OK,
			"compile program with Poco-owned allocator");
	}
	if (ok) {
		ok &= check(poco_vm_run(vm, program, &run_options, &result) == POCO_STATUS_OK,
			"run program with Poco-owned allocator");
		ok &= check(result == 37, "program result");
	}

	poco_program_destroy(program);
	poco_vm_destroy(vm);
	ok &= check(host_allocator_calls == 0,
		"Poco must not call Animator's allocator symbols");
	return ok ? 0 : 1;
}
