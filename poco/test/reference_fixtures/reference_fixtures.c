#include <poco/poco.h>

#include "poco_errcodes.h"
#include "reference_outputs.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

/* Private test seam: native bindings normally reach these through Poco's
 * standard-library implementation.  Keep this fixture independent of the
 * full compiler-private header (and its private libffi include path). */
void poco_set_error(PocoVm* vm, const char* format, ...);
Errcode* poco_vm_builtin_error(PocoVm* vm);

#ifndef POCO_REFERENCE_FIXTURE_DIR
#error "POCO_REFERENCE_FIXTURE_DIR must name the reference fixture directory"
#endif

static char diagnostic_message[512];
static pthread_mutex_t diagnostic_mutex = PTHREAD_MUTEX_INITIALIZER;

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
} ConcurrentRun;

static void gate_wait(ThreadGate* gate)
{
	pthread_mutex_lock(&gate->mutex);
	++gate->waiting;
	if (gate->waiting == 2)
		pthread_cond_broadcast(&gate->condition);
	else
		while (gate->waiting < 2)
			pthread_cond_wait(&gate->condition, &gate->mutex);
	pthread_mutex_unlock(&gate->mutex);
}

static int reference_identity(int value)
{
	return value;
}

static int reference_error(PocoVm* vm, const char* message)
{
	Errcode* status = poco_vm_builtin_error(vm);

	poco_set_error(vm, "%s", message);
	if (status != NULL)
		*status = Err_poco_ffi_invalid_binding;
	return 0;
}

static int reference_alpha_error(PocoVm* vm)
{
	return reference_error(vm, "alpha runtime error");
}

static int reference_beta_error(PocoVm* vm)
{
	return reference_error(vm, "beta runtime error");
}

static void collect_diagnostic(void* user_data, const PocoDiagnostic* diagnostic)
{
	(void)user_data;
	pthread_mutex_lock(&diagnostic_mutex);
	if (diagnostic->message != NULL) {
		strncpy(diagnostic_message, diagnostic->message, sizeof(diagnostic_message) - 1);
		diagnostic_message[sizeof(diagnostic_message) - 1] = '\0';
	}
	pthread_mutex_unlock(&diagnostic_mutex);
}

static int check(int condition, const char* context, const char* script_name, int iteration,
				 int32_t expected, int32_t actual, PocoStatus status)
{
	if (condition) {
		return 1;
	}

	fprintf(stderr,
			"poco_reference_fixtures: %s: script=%s iteration=%d "
			"status=%d expected=%d actual=%d diagnostic=%s\n",
			context, script_name, iteration, status, (int)expected, (int)actual,
			diagnostic_message[0] != '\0' ? diagnostic_message : "(none)");
	return 0;
}

static int compile_reference(const char* script_name, PocoVm** out_vm, PocoProgram** out_program)
{
	static const PocoBinding bindings[] = {
		{"int reference_identity(int value);", (PocoNativeFunction)reference_identity, NULL},
		{"int reference_alpha_error(void);", (PocoNativeFunction)reference_alpha_error,
			NULL, POCO_BINDING_RUN_CONTEXT},
		{"int reference_beta_error(void);", (PocoNativeFunction)reference_beta_error,
			NULL, POCO_BINDING_RUN_CONTEXT},
	};
	static const PocoLibrary library = {
		"test.reference", bindings, 3, NULL, NULL, NULL,
	};
	PocoVmOptions options = {0};
	char script_path[1024];
	PocoStatus status;

	options.diagnostic_callback = collect_diagnostic;
	diagnostic_message[0] = '\0';
	if (snprintf(script_path, sizeof(script_path), "%s/%s", POCO_REFERENCE_FIXTURE_DIR,
				 script_name) >= (int)sizeof(script_path)) {
		return check(0, "fixture path too long", script_name, 0, 0, 0,
					 POCO_STATUS_DIRECTORY_TOO_LONG);
	}

	status = poco_vm_create(&options, out_vm);
	if (!check(status == POCO_STATUS_OK, "create VM", script_name, 0, 0, 0, status)) {
		return 0;
	}
	status = poco_vm_register_library(*out_vm, &library);
	if (!check(status == POCO_STATUS_OK, "register library", script_name, 0, 0, 0, status)) {
		poco_vm_destroy(*out_vm);
		*out_vm = NULL;
		return 0;
	}

	status = poco_vm_compile_file(*out_vm, script_path, out_program);
	if (!check(status == POCO_STATUS_OK, "compile", script_name, 0, 0, 0, status)) {
		poco_vm_destroy(*out_vm);
		*out_vm = NULL;
		return 0;
	}
	return 1;
}

static int run_repeated_reference(void)
{
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	int iteration;
	int ok = 1;

	if (!compile_reference(POCO_REFERENCE_REPEATED_SCRIPT, &vm, &program)) {
		return 0;
	}

	for (iteration = 0; iteration < POCO_REFERENCE_REPEAT_COUNT; ++iteration) {
		int32_t result = 0;
		PocoStatus status = poco_vm_run(vm, program, NULL, &result);
		if (!check(status == POCO_STATUS_OK && result == POCO_REFERENCE_REPEATED_RESULT,
				   "repeated run", POCO_REFERENCE_REPEATED_SCRIPT, iteration,
				   POCO_REFERENCE_REPEATED_RESULT, result, status)) {
			ok = 0;
			break;
		}
	}

	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return ok;
}

static int run_distinct_references(void)
{
	size_t fixture_index;

	for (fixture_index = 0; fixture_index < poco_reference_output_count; ++fixture_index) {
		const PocoReferenceOutput* reference = &poco_reference_outputs[fixture_index];
		PocoVm* vm = NULL;
		PocoProgram* program = NULL;
		int32_t result = 0;
		PocoStatus status;
		size_t earlier_index;

		for (earlier_index = 0; earlier_index < fixture_index; ++earlier_index) {
			if (!check(reference->expected_result !=
						   poco_reference_outputs[earlier_index].expected_result,
					   "reference outputs must be distinct", reference->script_name, 0,
					   reference->expected_result,
					   poco_reference_outputs[earlier_index].expected_result, POCO_STATUS_OK)) {
				return 0;
			}
		}

		if (!compile_reference(reference->script_name, &vm, &program)) {
			return 0;
		}
		status = poco_vm_run(vm, program, NULL, &result);
		if (!check(status == POCO_STATUS_OK && result == reference->expected_result, "distinct run",
				   reference->script_name, 0, reference->expected_result, result, status)) {
			poco_program_destroy(program);
			poco_vm_destroy(vm);
			return 0;
		}

		poco_program_destroy(program);
		poco_vm_destroy(vm);
	}
	return 1;
}

static void* run_concurrently(void* data)
{
	ConcurrentRun* run = data;

	gate_wait(run->gate);
	run->status = poco_vm_run(run->vm, run->program, NULL, &run->result);
	return NULL;
}

static int run_concurrency_gate(void)
{
	ThreadGate gate = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0};
	ConcurrentRun runs[2] = {{0}};
	pthread_t threads[2];
	int ok;

	/* The Phase-3 gate deliberately shares both VM and compiled program.  Each
	 * call must manufacture its own activation rather than serializing through
	 * either object or writing callback/library state into compiled frames. */
	if (!compile_reference("arithmetic.poc", &runs[0].vm, &runs[0].program))
		return 0;
	runs[1].vm = runs[0].vm;
	runs[1].program = runs[0].program;
	runs[0].gate = runs[1].gate = &gate;
	pthread_create(&threads[0], NULL, run_concurrently, &runs[0]);
	pthread_create(&threads[1], NULL, run_concurrently, &runs[1]);
	pthread_join(threads[0], NULL);
	pthread_join(threads[1], NULL);
	ok = runs[0].status == POCO_STATUS_OK && runs[0].result == 17 &&
		runs[1].status == POCO_STATUS_OK && runs[1].result == 17;
	poco_program_destroy(runs[0].program);
	poco_vm_destroy(runs[0].vm);
	if (!ok)
		return 0;

	/* A failing run also publishes its activation-local diagnostic after the
	 * interpreter exits.  Exercise that path with the same sharing boundary so
	 * TSan covers all run-exit writes, not only successful result delivery. */
	memset(runs, 0, sizeof(runs));
	if (!compile_reference("error_alpha.poc", &runs[0].vm, &runs[0].program))
		return 0;
	runs[1].vm = runs[0].vm;
	runs[1].program = runs[0].program;
	gate.waiting = 0;
	runs[0].gate = runs[1].gate = &gate;
	pthread_create(&threads[0], NULL, run_concurrently, &runs[0]);
	pthread_create(&threads[1], NULL, run_concurrently, &runs[1]);
	pthread_join(threads[0], NULL);
	pthread_join(threads[1], NULL);
	ok = runs[0].status != POCO_STATUS_OK && runs[1].status != POCO_STATUS_OK &&
		strstr(poco_get_last_error(runs[0].vm), "alpha runtime error") != NULL;
	poco_program_destroy(runs[0].program);
	poco_vm_destroy(runs[0].vm);
	if (!ok)
		return 0;

	memset(runs, 0, sizeof(runs));
	if (!compile_reference("error_alpha.poc", &runs[0].vm, &runs[0].program) ||
		!compile_reference("error_beta.poc", &runs[1].vm, &runs[1].program))
		return 0;
	gate.waiting = 0;
	runs[0].gate = runs[1].gate = &gate;
	pthread_create(&threads[0], NULL, run_concurrently, &runs[0]);
	pthread_create(&threads[1], NULL, run_concurrently, &runs[1]);
	pthread_join(threads[0], NULL);
	pthread_join(threads[1], NULL);
	ok = runs[0].status != POCO_STATUS_OK && runs[1].status != POCO_STATUS_OK &&
		strstr(poco_get_last_error(runs[0].vm), "alpha runtime error") != NULL &&
		strstr(poco_get_last_error(runs[1].vm), "beta runtime error") != NULL;
	poco_program_destroy(runs[0].program);
	poco_program_destroy(runs[1].program);
	poco_vm_destroy(runs[0].vm);
	poco_vm_destroy(runs[1].vm);
	pthread_mutex_destroy(&gate.mutex);
	pthread_cond_destroy(&gate.condition);
	return ok;
}

int main(void)
{
	if (!run_repeated_reference() || !run_distinct_references() || !run_concurrency_gate()) {
		return 1;
	}
	return 0;
}
