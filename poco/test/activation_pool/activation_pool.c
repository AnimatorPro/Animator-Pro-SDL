#include <poco/poco.h>

#include "reference_outputs.h"

#include <pthread.h>
#include <stdio.h>

#ifndef POCO_ACTIVATION_POOL_FIXTURE_DIR
#error "POCO_ACTIVATION_POOL_FIXTURE_DIR must name the activation fixture directory"
#endif

enum { POCO_POOL_THREAD_COUNT = 4 };

typedef struct ThreadGate
{
	pthread_mutex_t mutex;
	pthread_cond_t condition;
	int waiting;
} ThreadGate;

typedef struct PoolRun
{
	ThreadGate* gate;
	PocoProgram* program;
	PocoStatus status;
	int iteration;
	int32_t result;
} PoolRun;

static int reference_identity(int value)
{
	return value;
}

static int check(int condition, const char* context, PocoStatus status)
{
	if (condition)
		return 1;
	fprintf(stderr, "poco_activation_pool: %s (status=%d)\n", context, status);
	return 0;
}

static void gate_wait(ThreadGate* gate)
{
	pthread_mutex_lock(&gate->mutex);
	++gate->waiting;
	if (gate->waiting == POCO_POOL_THREAD_COUNT)
		pthread_cond_broadcast(&gate->condition);
	else
		while (gate->waiting < POCO_POOL_THREAD_COUNT)
			pthread_cond_wait(&gate->condition, &gate->mutex);
	pthread_mutex_unlock(&gate->mutex);
}

static void* run_from_pool(void* data)
{
	PoolRun* run = data;
	PocoActivation* activation = NULL;
	int iteration;

	gate_wait(run->gate);
	run->status = poco_activation_acquire(run->program, &activation);
	if (run->status != POCO_STATUS_OK)
		return NULL;
	/* Resetting a newly acquired activation is valid and allocation-free. */
	run->status = poco_activation_reset(activation);
	for (iteration = 0;
		iteration < POCO_REFERENCE_REPEAT_COUNT && run->status == POCO_STATUS_OK;
		++iteration) {
		run->result = 0;
		run->status = poco_activation_run(activation, NULL, &run->result);
		run->iteration = iteration;
		if (run->status != POCO_STATUS_OK ||
			run->result != POCO_REFERENCE_REPEATED_RESULT)
			break;
		if (iteration + 1 < POCO_REFERENCE_REPEAT_COUNT)
			run->status = poco_activation_reset(activation);
	}
	if (run->status == POCO_STATUS_OK) {
		/* A completed activation cannot accidentally leak state into a second
		 * run when a host forgets to return it through reset. */
		run->status = poco_activation_run(activation, NULL, NULL);
		if (run->status == POCO_STATUS_PARAMETER_RANGE)
			run->status = POCO_STATUS_OK;
		else
			run->status = POCO_STATUS_INTERNAL_ERROR;
	}
	poco_activation_release(activation);
	return NULL;
}

int main(void)
{
	static const PocoBinding bindings[] = {
		{"int reference_identity(int value);",
			(PocoNativeFunction)reference_identity, NULL, 0},
	};
	static const PocoLibrary library = {
		"test.activation_pool", bindings, 1, NULL, NULL, NULL,
	};
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	PocoActivation* activation = NULL;
	PocoStatus status;
	char script_path[1024];
	int32_t reference_result = 0;
	ThreadGate gate = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0};
	PoolRun runs[POCO_POOL_THREAD_COUNT] = {{0}};
	pthread_t threads[POCO_POOL_THREAD_COUNT];
	int index;
	int ok = 1;

	poco_activation_release(NULL);
	if (!check(poco_activation_acquire(NULL, &activation) ==
			POCO_STATUS_NULL_REFERENCE,
			"reject null program", POCO_STATUS_NULL_REFERENCE) ||
		!check(poco_activation_reset(NULL) == POCO_STATUS_NULL_REFERENCE,
			"reject null activation reset", POCO_STATUS_NULL_REFERENCE) ||
		!check(poco_activation_run(NULL, NULL, NULL) ==
			POCO_STATUS_NULL_REFERENCE,
			"reject null activation run", POCO_STATUS_NULL_REFERENCE))
		return 1;

	if (snprintf(script_path, sizeof(script_path), "%s/%s",
			POCO_ACTIVATION_POOL_FIXTURE_DIR, POCO_REFERENCE_REPEATED_SCRIPT) >=
		(int)sizeof(script_path))
		return 1;
	status = poco_vm_create(NULL, &vm);
	if (!check(status == POCO_STATUS_OK, "create VM", status))
		return 1;
	status = poco_vm_register_library(vm, &library);
	if (!check(status == POCO_STATUS_OK, "register library", status)) {
		ok = 0;
		goto CLEANUP;
	}
	status = poco_vm_compile_file(vm, script_path, &program);
	if (!check(status == POCO_STATUS_OK, "compile once", status)) {
		ok = 0;
		goto CLEANUP;
	}
	status = poco_vm_run(vm, program, NULL, &reference_result);
	if (!check(status == POCO_STATUS_OK &&
			reference_result == POCO_REFERENCE_REPEATED_RESULT,
			"single-threaded reference", status)) {
		ok = 0;
		goto CLEANUP;
	}

	for (index = 0; index < POCO_POOL_THREAD_COUNT; ++index) {
		runs[index].gate = &gate;
		runs[index].program = program;
		pthread_create(&threads[index], NULL, run_from_pool, &runs[index]);
	}
	for (index = 0; index < POCO_POOL_THREAD_COUNT; ++index)
		pthread_join(threads[index], NULL);
	for (index = 0; index < POCO_POOL_THREAD_COUNT; ++index) {
		if (runs[index].status != POCO_STATUS_OK ||
			runs[index].iteration != POCO_REFERENCE_REPEAT_COUNT - 1 ||
			runs[index].result != reference_result) {
			fprintf(stderr,
				"poco_activation_pool: thread=%d iteration=%d status=%d "
				"expected=%d actual=%d\n",
				index, runs[index].iteration, runs[index].status,
				(int)reference_result, (int)runs[index].result);
			ok = 0;
		}
	}

CLEANUP:
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	pthread_mutex_destroy(&gate.mutex);
	pthread_cond_destroy(&gate.condition);
	return ok ? 0 : 1;
}
