/*
 * Two VMs, two threads, one status slot per activation.
 *
 * Animator's bindings take no PocoVm parameter, so they cannot reach their
 * caller's activation through an argument; they report through
 * poco_active_builtin_error(), which resolves to the activation running on the
 * calling thread.  This fixture holds that contract from the outside:
 *
 *   isolation - both VMs hammer the slot concurrently with their own tag and
 *               read it back.  A process-global slot loses the race and the
 *               script counts the mismatches.
 *   delivery  - a vm-less binding that reports a failure aborts its own run,
 *               and a second VM running clean code beside it still succeeds.
 *               A process-global slot would either drop the failure or spread
 *               it to the innocent VM.
 *   detached  - calling the accessor with no run in progress is safe.
 */

#include <poco/poco.h>

#include "poco_errcodes.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

/* Private test seam, as in reference_fixtures: keep the fixture off the
 * compiler-private header and its libffi include path. */
Errcode* poco_active_builtin_error(void);

#ifndef POCO_VM_SCOPED_BUILTIN_ERROR_FIXTURE_DIR
#error "POCO_VM_SCOPED_BUILTIN_ERROR_FIXTURE_DIR must name the fixture directory"
#endif

#define STAMP_ITERATIONS 2000

static _Thread_local int thread_tag;

typedef struct ThreadGate {
	pthread_mutex_t mutex;
	pthread_cond_t condition;
	int waiting;
} ThreadGate;

typedef struct ConcurrentRun {
	ThreadGate* gate;
	PocoVm* vm;
	PocoProgram* program;
	PocoStatus status;
	int32_t result;
	int tag;
} ConcurrentRun;

static void gate_wait(ThreadGate* gate)
{
	pthread_mutex_lock(&gate->mutex);
	++gate->waiting;
	if (gate->waiting == 2) {
		pthread_cond_broadcast(&gate->condition);
	} else {
		while (gate->waiting < 2) {
			pthread_cond_wait(&gate->condition, &gate->mutex);
		}
	}
	pthread_mutex_unlock(&gate->mutex);
}

/* Bindings below deliberately take no PocoVm*: that is the Animator shape. */

static int ani_tag(void)
{
	return thread_tag;
}

static int ani_stamp(int value)
{
	Errcode* slot = poco_active_builtin_error();
	volatile int spin;
	int observed;

	/* Positive values are not failures, so the interpreter keeps running and
	 * the slot stays observable across the whole window. */
	*slot = (Errcode)value;
	for (spin = 0; spin < 64; ++spin) {
	}
	observed = (int)*slot;
	*slot = Success;
	return observed;
}

static void ani_fail(void)
{
	*poco_active_builtin_error() = Err_null_ref;
}

static int failures;

static int check(int condition, const char* context)
{
	if (!condition) {
		fprintf(stderr, "poco_vm_scoped_builtin_error: %s\n", context);
		++failures;
	}
	return condition;
}

static int compile_fixture(const char* script_name, PocoVm** out_vm, PocoProgram** out_program)
{
	static const PocoBinding bindings[] = {
		{"int ani_tag(void);", (PocoNativeFunction)ani_tag, NULL, 0},
		{"int ani_stamp(int value);", (PocoNativeFunction)ani_stamp, NULL, 0},
		{"void ani_fail(void);", (PocoNativeFunction)ani_fail, NULL, 0},
	};
	static const PocoLibrary library = {
		"test.vm_scoped_builtin_error", bindings, 3, NULL, NULL, NULL,
	};
	PocoVmOptions options = {0};
	char script_path[1024];
	PocoStatus status;

	*out_vm = NULL;
	*out_program = NULL;
	if (snprintf(script_path, sizeof(script_path), "%s/%s",
				 POCO_VM_SCOPED_BUILTIN_ERROR_FIXTURE_DIR, script_name) >= (int)sizeof(script_path)) {
		return check(0, "fixture path too long");
	}
	status = poco_vm_create(&options, out_vm);
	if (!check(status == POCO_STATUS_OK, "create VM")) {
		return 0;
	}
	status = poco_vm_register_library(*out_vm, &library);
	if (!check(status == POCO_STATUS_OK, "register library")) {
		poco_vm_destroy(*out_vm);
		*out_vm = NULL;
		return 0;
	}
	status = poco_vm_compile_file(*out_vm, script_path, out_program);
	if (!check(status == POCO_STATUS_OK, "compile fixture")) {
		poco_vm_destroy(*out_vm);
		*out_vm = NULL;
		return 0;
	}
	return 1;
}

static void* run_concurrently(void* data)
{
	ConcurrentRun* run = data;

	thread_tag = run->tag;
	gate_wait(run->gate);
	run->status = poco_vm_run(run->vm, run->program, NULL, &run->result);
	return NULL;
}

static void release(ConcurrentRun* run)
{
	poco_program_destroy(run->program);
	poco_vm_destroy(run->vm);
	run->program = NULL;
	run->vm = NULL;
}

/* Two VMs write the slot concurrently; neither may see the other's tag. */
static void test_isolation(void)
{
	ThreadGate gate = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0};
	ConcurrentRun runs[2] = {{0}};
	pthread_t threads[2];

	if (!compile_fixture("stamp.poc", &runs[0].vm, &runs[0].program) ||
		!compile_fixture("stamp.poc", &runs[1].vm, &runs[1].program)) {
		return;
	}
	runs[0].gate = runs[1].gate = &gate;
	runs[0].tag = 0x5a5a;
	runs[1].tag = 0x2727;
	pthread_create(&threads[0], NULL, run_concurrently, &runs[0]);
	pthread_create(&threads[1], NULL, run_concurrently, &runs[1]);
	pthread_join(threads[0], NULL);
	pthread_join(threads[1], NULL);

	check(runs[0].status == POCO_STATUS_OK && runs[1].status == POCO_STATUS_OK,
		  "both stamp runs must succeed");
	check(runs[0].result == 0 && runs[1].result == 0,
		  "each VM must read back only its own tag from the builtin status slot");
	release(&runs[0]);
	release(&runs[1]);
}

/* One VM reports a failure from a vm-less binding while the other runs clean
 * code.  The failure must land on the reporting VM and only on it. */
static void test_delivery(void)
{
	ThreadGate gate = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0};
	ConcurrentRun runs[2] = {{0}};
	pthread_t threads[2];

	if (!compile_fixture("fail.poc", &runs[0].vm, &runs[0].program) ||
		!compile_fixture("stamp.poc", &runs[1].vm, &runs[1].program)) {
		return;
	}
	runs[0].gate = runs[1].gate = &gate;
	runs[0].tag = 0x1111;
	runs[1].tag = 0x2222;
	pthread_create(&threads[0], NULL, run_concurrently, &runs[0]);
	pthread_create(&threads[1], NULL, run_concurrently, &runs[1]);
	pthread_join(threads[0], NULL);
	pthread_join(threads[1], NULL);

	check(runs[0].status != POCO_STATUS_OK,
		  "a vm-less binding's failure must abort its own run");
	check(runs[0].result != 7, "the aborted run must not deliver its return value");
	check(runs[1].status == POCO_STATUS_OK && runs[1].result == 0,
		  "the other VM must not inherit the failure");
	release(&runs[0]);
	release(&runs[1]);
}

/* The accessor is also reachable outside a run; it must not fault there. */
static void test_detached(void)
{
	Errcode* slot = poco_active_builtin_error();

	if (check(slot != NULL, "detached status slot must exist")) {
		*slot = Err_null_ref;
		check(*slot == Err_null_ref, "detached status slot must be writable");
		*slot = Success;
	}
}

int main(void)
{
	test_detached();
	test_isolation();
	test_delivery();
	test_detached();
	if (failures != 0) {
		fprintf(stderr, "poco_vm_scoped_builtin_error: %d check(s) failed\n", failures);
		return 1;
	}
	return 0;
}
