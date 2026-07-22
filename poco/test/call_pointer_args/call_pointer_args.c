#include <poco/poco.h>

#include <stdint.h>
#include <stdio.h>

#ifndef POCO_CALL_POINTER_ARGS_FIXTURE_DIR
#error "POCO_CALL_POINTER_ARGS_FIXTURE_DIR must name the pointer-call fixture directory"
#endif

static int failures;

static int check(int condition, const char* context)
{
	if (condition) {
		return 1;
	}
	fprintf(stderr, "poco_call_pointer_args: %s\n", context);
	++failures;
	return 0;
}

static PocoStatus begin_call(PocoActivation* activation, const char* name, PocoCall** out_call)
{
	*out_call = NULL;
	return poco_call_begin(activation, name, out_call);
}

int main(void)
{
	PocoVm* vm = NULL;
	PocoProgram* program = NULL;
	PocoActivation* activation = NULL;
	PocoCall* call = NULL;
	PocoCallbackValue result;
	int writable_values[] = {4, 5, 6};
	int read_only_values[] = {7, 8, 9};
	int narrow_values[] = {11, 12};
	int externally_registered[] = {20, 21, 22};
	char path[1024];
	int ok = 1;

	ok &= check(poco_call_push_pointer(NULL, writable_values, sizeof(writable_values),
									   POCO_POINTER_PERMISSION_READ) == POCO_STATUS_NULL_REFERENCE,
				"null call is rejected");
	if (snprintf(path, sizeof(path), "%s/call_pointer_args.poc",
				 POCO_CALL_POINTER_ARGS_FIXTURE_DIR) >= (int)sizeof(path)) {
		return 1;
	}
	ok &= check(poco_vm_create(NULL, &vm) == POCO_STATUS_OK, "create VM");
	ok &= check(poco_vm_compile_file(vm, path, &program) == POCO_STATUS_OK, "compile fixture");
	ok &= check(poco_activation_acquire(program, &activation) == POCO_STATUS_OK,
				"acquire activation");
	ok &= check(poco_activation_init(activation) == POCO_STATUS_OK, "initialize activation");

	ok &= check(begin_call(activation, "write_value", &call) == POCO_STATUS_OK,
				"begin writable-span call");
	ok &= check(poco_call_push_pointer(
					call, writable_values, sizeof(writable_values),
					POCO_POINTER_PERMISSION_READ | POCO_POINTER_PERMISSION_WRITE) == POCO_STATUS_OK,
				"push writable span");
	ok &= check(poco_call_push_int(call, 1) == POCO_STATUS_OK &&
					poco_call_push_int(call, 42) == POCO_STATUS_OK,
				"push writable-span scalar arguments");
	ok &= check(poco_call_invoke(call, &result) == POCO_STATUS_OK &&
					result.kind == POCO_CALLBACK_VALUE_INT && result.value.int_value == 42 &&
					writable_values[0] == 4 && writable_values[1] == 42 && writable_values[2] == 6,
				"writable span round-trips within exact bounds");
	ok &= check(poco_vm_unregister_borrowed_span(vm, writable_values) == POCO_STATUS_NOT_FOUND,
				"call-owned registration is released after invoke");
	poco_call_end(call);
	call = NULL;

	ok &= check(begin_call(activation, "remember_values", &call) == POCO_STATUS_OK &&
					poco_call_push_pointer(call, writable_values, sizeof(writable_values),
										   POCO_POINTER_PERMISSION_READ) == POCO_STATUS_OK &&
					poco_call_invoke(call, &result) == POCO_STATUS_OK &&
					result.kind == POCO_CALLBACK_VALUE_INT && result.value.int_value == 4,
				"script may use a borrowed pointer during invoke");
	poco_call_end(call);
	call = NULL;
	ok &= check(begin_call(activation, "read_saved_value", &call) == POCO_STATUS_OK &&
					poco_call_invoke(call, &result) == POCO_STATUS_FFI_BOUNDS,
				"escaped borrowed pointer becomes stale after invoke");
	poco_call_end(call);
	call = NULL;

	ok &= check(begin_call(activation, "read_values", &call) == POCO_STATUS_OK &&
					poco_call_push_pointer(call, read_only_values, sizeof(read_only_values),
										   POCO_POINTER_PERMISSION_READ) == POCO_STATUS_OK &&
					poco_call_invoke(call, &result) == POCO_STATUS_OK &&
					result.kind == POCO_CALLBACK_VALUE_INT && result.value.int_value == 16,
				"read-only span permits in-bounds reads");
	poco_call_end(call);
	call = NULL;

	ok &= check(begin_call(activation, "write_value", &call) == POCO_STATUS_OK &&
					poco_call_push_pointer(call, read_only_values, sizeof(read_only_values),
										   POCO_POINTER_PERMISSION_READ) == POCO_STATUS_OK &&
					poco_call_push_int(call, 1) == POCO_STATUS_OK &&
					poco_call_push_int(call, 99) == POCO_STATUS_OK &&
					poco_call_invoke(call, &result) == POCO_STATUS_FFI_BOUNDS &&
					result.kind == POCO_CALLBACK_VALUE_INVALID && read_only_values[1] == 8,
				"read-only span rejects script writes without modifying the host buffer");
	poco_call_end(call);
	call = NULL;

	ok &= check(begin_call(activation, "write_value", &call) == POCO_STATUS_OK &&
					poco_call_push_pointer(call, narrow_values, sizeof(int),
										   POCO_POINTER_PERMISSION_READ |
											   POCO_POINTER_PERMISSION_WRITE) == POCO_STATUS_OK &&
					poco_call_push_int(call, 1) == POCO_STATUS_OK &&
					poco_call_push_int(call, 77) == POCO_STATUS_OK &&
					poco_call_invoke(call, &result) == POCO_STATUS_FFI_BOUNDS &&
					narrow_values[0] == 11 && narrow_values[1] == 12,
				"out-of-span write is rejected before touching adjacent memory");
	poco_call_end(call);
	call = NULL;

	ok &= check(begin_call(activation, "read_values", &call) == POCO_STATUS_OK &&
					poco_call_push_pointer(call, narrow_values, 2 * sizeof(int),
										   POCO_POINTER_PERMISSION_READ) == POCO_STATUS_OK &&
					poco_call_invoke(call, &result) == POCO_STATUS_FFI_BOUNDS,
				"out-of-span read is rejected");
	poco_call_end(call);
	call = NULL;

	ok &= check(begin_call(activation, "add_one", &call) == POCO_STATUS_OK &&
					poco_call_push_pointer(call, writable_values, sizeof(writable_values),
										   POCO_POINTER_PERMISSION_READ) == POCO_STATUS_OK &&
					poco_call_invoke(call, &result) == POCO_STATUS_PARAMETER_RANGE,
				"pointer argument against numeric parameter is rejected at invoke");
	poco_call_end(call);
	call = NULL;

	ok &= check(begin_call(activation, "read_values", &call) == POCO_STATUS_OK &&
					poco_call_push_int(call, 1) == POCO_STATUS_OK &&
					poco_call_invoke(call, &result) == POCO_STATUS_PARAMETER_RANGE,
				"numeric argument against pointer parameter is rejected at invoke");
	poco_call_end(call);
	call = NULL;

	ok &= check(begin_call(activation, "read_values", &call) == POCO_STATUS_OK,
				"begin invalid-span call");
	ok &= check(
		poco_call_push_pointer(call, NULL, sizeof(int), POCO_POINTER_PERMISSION_READ) ==
				POCO_STATUS_PARAMETER_RANGE &&
			poco_call_push_pointer(call, writable_values, 0, POCO_POINTER_PERMISSION_READ) ==
				POCO_STATUS_PARAMETER_RANGE &&
			poco_call_push_pointer(call, writable_values, sizeof(writable_values),
								   POCO_POINTER_PERMISSION_NONE) == POCO_STATUS_PARAMETER_RANGE,
		"null, empty, and permissionless spans are rejected without adding arguments");
	poco_call_end(call);
	call = NULL;

	ok &= check(
		poco_vm_register_borrowed_span(vm, externally_registered, sizeof(externally_registered),
									   POCO_POINTER_PERMISSION_READ) == POCO_STATUS_OK,
		"register external span before builder use");
	ok &=
		check(begin_call(activation, "read_values", &call) == POCO_STATUS_OK &&
				  poco_call_push_pointer(call, externally_registered, sizeof(externally_registered),
										 POCO_POINTER_PERMISSION_READ) == POCO_STATUS_OK &&
				  poco_call_invoke(call, &result) == POCO_STATUS_OK &&
				  result.kind == POCO_CALLBACK_VALUE_INT && result.value.int_value == 42,
			  "builder reuses a compatible existing borrowed registration");
	poco_call_end(call);
	call = NULL;
	ok &= check(poco_vm_unregister_borrowed_span(vm, externally_registered) == POCO_STATUS_OK,
				"builder does not unregister a host-owned registration");
	ok &= check(poco_vm_register_borrowed_span(
					vm, externally_registered, sizeof(externally_registered),
					POCO_POINTER_PERMISSION_READ | POCO_POINTER_PERMISSION_WRITE) == POCO_STATUS_OK,
				"register broader external permissions");
	ok &=
		check(begin_call(activation, "write_value", &call) == POCO_STATUS_OK &&
				  poco_call_push_pointer(call, externally_registered, sizeof(externally_registered),
										 POCO_POINTER_PERMISSION_READ) == POCO_STATUS_OK &&
				  poco_call_push_int(call, 0) == POCO_STATUS_OK &&
				  poco_call_push_int(call, 88) == POCO_STATUS_OK &&
				  poco_call_invoke(call, &result) == POCO_STATUS_FFI_BOUNDS &&
				  externally_registered[0] == 20,
			  "call-local read intent narrows a broader VM registration");
	poco_call_end(call);
	call = NULL;
	ok &= check(poco_vm_unregister_borrowed_span(vm, externally_registered) == POCO_STATUS_OK,
				"broader host registration remains after narrowed call");

	poco_activation_release(activation);
	poco_program_destroy(program);
	poco_vm_destroy(vm);
	return ok && failures == 0 ? 0 : 1;
}
