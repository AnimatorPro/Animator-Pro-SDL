/*
 * Two VMs, one after the other, on one thread.
 *
 * The standard file and memory libraries track the resources a script opens or
 * allocates so they can be released when the run ends.  Those lists belong to
 * the VM, not to the process-global Poco_lib descriptor the VM cloned: a list
 * reachable without a VM outlives the run that filled it, and the next VM to
 * start on the thread inherits whatever the previous one left behind.
 *
 * The fixture holds that from the outside, using only the VM-less legacy
 * allocation entry points, which is the shape Animator's bindings have:
 *
 *   handoff  - the first VM allocates and abandons a block.  Once it is gone,
 *              a second VM asking to release that block must be told it does
 *              not own it.  Against a shared list the release succeeds, which
 *              is one VM freeing another's memory.
 *   detached - the same release attempted with no run in progress must fail
 *              too.  There is no list to find outside a VM.
 *   roundtrip - allocate and release inside one run: the release must succeed,
 *              so the two checks above cannot pass by failing everything.
 */

#include <poco/poco.h>

#include "poco_errcodes.h"

#include <stdio.h>

/* Private test seam, as in vm_scoped_builtin_error: the legacy VM-less entry
 * points live on the compat header, which the fixture does not include. */
void* po_malloc(int size);
void po_free(void* pt);
Errcode* poco_active_builtin_error(void);

#ifndef POCO_SERIAL_VM_LIBRARY_STATE_FIXTURE_DIR
#error "POCO_SERIAL_VM_LIBRARY_STATE_FIXTURE_DIR must name the fixture directory"
#endif

#define BLOCK_SIZE 64

static void* abandoned_block;
static int failures;

static int check(int condition, const char* context)
{
	if (!condition) {
		fprintf(stderr, "poco_serial_vm_library_state: %s\n", context);
		++failures;
	}
	return condition;
}

/*
 * Reports what the release said rather than returning void, so the script can
 * hand the Errcode back to the host as the run's result.  The slot is cleared
 * again on the way out: a failure left in it aborts the run, and here a refused
 * release is the answer the test wants, not an error.
 */
static int release_block(void* pointer)
{
	Errcode* slot = poco_active_builtin_error();
	Errcode reported;

	*slot = Success;
	po_free(pointer);
	reported = *slot;
	*slot = Success;
	return (int)reported;
}

/* Bindings below deliberately take no PocoVm*: that is the Animator shape. */

static int leak_block(void)
{
	abandoned_block = po_malloc(BLOCK_SIZE);
	return abandoned_block != NULL ? Success : Err_no_memory;
}

static int release_stale_block(void)
{
	return release_block(abandoned_block);
}

static int allocate_and_release_block(void)
{
	void* block = po_malloc(BLOCK_SIZE);

	if (block == NULL) {
		return Err_no_memory;
	}
	return release_block(block);
}

static int compile_fixture(const char* script_name, PocoVm** out_vm, PocoProgram** out_program)
{
	static const PocoBinding bindings[] = {
		{"int leak_block(void);", (PocoNativeFunction)leak_block, NULL, 0},
		{"int release_stale_block(void);", (PocoNativeFunction)release_stale_block, NULL, 0},
		{"int allocate_and_release_block(void);", (PocoNativeFunction)allocate_and_release_block,
		 NULL, 0},
	};
	static const PocoLibrary library = {
		"test.serial_vm_library_state", bindings, 3, NULL, NULL, NULL,
	};
	PocoVmOptions options = {0};
	char script_path[1024];
	PocoStatus status;

	*out_vm = NULL;
	*out_program = NULL;
	if (snprintf(script_path, sizeof(script_path), "%s/%s",
				 POCO_SERIAL_VM_LIBRARY_STATE_FIXTURE_DIR, script_name) >= (int)sizeof(script_path)) {
		return check(0, "fixture path too long");
	}
	status = poco_vm_create(&options, out_vm);
	if (!check(status == POCO_STATUS_OK, "create VM")) {
		return 0;
	}
	/* malloc/free reach the script through the standard memory library, and
	 * the VM-less entry points resolve to whichever copy of it this VM owns. */
	status = poco_vm_register_standard_library(*out_vm);
	if (!check(status == POCO_STATUS_OK, "register standard library")) {
		goto FAILED;
	}
	status = poco_vm_register_library(*out_vm, &library);
	if (!check(status == POCO_STATUS_OK, "register fixture library")) {
		goto FAILED;
	}
	status = poco_vm_compile_file(*out_vm, script_path, out_program);
	if (!check(status == POCO_STATUS_OK, "compile fixture")) {
		goto FAILED;
	}
	return 1;

FAILED:
	poco_vm_destroy(*out_vm);
	*out_vm = NULL;
	return 0;
}

static int run_fixture(const char* script_name, int32_t* out_result)
{
	PocoProgram* program;
	PocoVm* vm;
	PocoStatus status;

	if (!compile_fixture(script_name, &vm, &program)) {
		return 0;
	}
	status = poco_vm_run(vm, program, NULL, out_result);
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return check(status == POCO_STATUS_OK, "run fixture");
}

/* A block the first VM abandoned must not be releasable by the second one. */
static void test_handoff(void)
{
	int32_t result = 0;

	abandoned_block = NULL;
	if (!run_fixture("leak.poc", &result)) {
		return;
	}
	if (!check(result == Success && abandoned_block != NULL, "first VM must allocate a block")) {
		return;
	}
	if (!run_fixture("probe.poc", &result)) {
		return;
	}
	check(result == Err_poco_free,
		  "the second VM must not own the block the first VM abandoned");
}

/* Nor by a caller with no run in progress: there is no list outside a VM. */
static void test_detached(void)
{
	int32_t result = 0;

	abandoned_block = NULL;
	if (!run_fixture("leak.poc", &result)) {
		return;
	}
	if (!check(result == Success && abandoned_block != NULL, "first VM must allocate a block")) {
		return;
	}
	check(release_block(abandoned_block) == Err_poco_free,
		  "a release outside any run must reach no resource list");
}

/* The negative checks above must not be passing because release never works. */
static void test_roundtrip(void)
{
	int32_t result = 0;

	if (!run_fixture("roundtrip.poc", &result)) {
		return;
	}
	check(result == Success, "allocate and release within one run must succeed");
}

int main(void)
{
	test_roundtrip();
	test_handoff();
	test_detached();
	test_roundtrip();
	if (failures != 0) {
		fprintf(stderr, "poco_serial_vm_library_state: %d check(s) failed\n", failures);
		return 1;
	}
	return 0;
}
