/*******************************************************************************
 * poco_call.c - the host-to-script call API.
 *
 * A PocoCall collects coerced arguments for one named function in an
 * activation, registers any borrowed spans it is handed for the duration of the
 * call, and releases them again whether the call succeeds or fails.
 ******************************************************************************/

#include "activation.h"
#include "poco_ffi.h"
#include "poco_internal.h"
#include "pocoface.h"
#include "pocotype.h"
#include "program_internal.h"
#include "vm_api.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct PocoCallPointerRegistration {
	void* pointer;
	size_t byte_count;
	uint32_t permissions;
} PocoCallPointerRegistration;

struct PocoCall {
	PocoActivation* activation;
	const Func_frame* function;
	PocoCallbackValue* values;
	PocoCallPointerRegistration* pointer_registrations;
	size_t value_count;
	size_t value_capacity;
	size_t registered_pointer_count;
	size_t registered_pointer_capacity;
	int invoked;
};

PocoStatus poco_call_begin(PocoActivation* activation, const char* name, PocoCall** out_call)
{
	const Func_frame* function;
	PocoCall* call;

	if (out_call == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	*out_call = NULL;
	if (activation == NULL || name == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (activation->program == NULL || activation->program->vm == NULL ||
		activation->program->vm->destroy_requested) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	function = po_activation_find_function(activation, name);
	if (function == NULL || function->code_pt == NULL || !function->got_code) {
		return POCO_STATUS_NOT_FOUND;
	}
	call = calloc(1, sizeof(*call));
	if (call == NULL) {
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	call->activation = activation;
	call->function = function;
	*out_call = call;
	return POCO_STATUS_OK;
}

static PocoStatus poco_call_push_value(PocoCall* call, PocoCallbackValue value)
{
	PocoCallbackValue* grown;
	size_t capacity;

	if (call == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (call->invoked) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	if (call->value_count == call->value_capacity) {
		capacity = call->value_capacity != 0 ? call->value_capacity * 2 : 4;
		if (capacity < call->value_capacity || capacity > SIZE_MAX / sizeof(*grown)) {
			return POCO_STATUS_OUT_OF_MEMORY;
		}
		grown = realloc(call->values, capacity * sizeof(*grown));
		if (grown == NULL) {
			return POCO_STATUS_OUT_OF_MEMORY;
		}
		call->values = grown;
		call->value_capacity = capacity;
	}
	call->values[call->value_count++] = value;
	return POCO_STATUS_OK;
}

PocoStatus poco_call_push_int(PocoCall* call, int value)
{
	PocoCallbackValue argument = {POCO_CALLBACK_VALUE_INT, {.int_value = value}};

	return poco_call_push_value(call, argument);
}

PocoStatus poco_call_push_long(PocoCall* call, long value)
{
	PocoCallbackValue argument = {POCO_CALLBACK_VALUE_LONG, {.long_value = value}};

	return poco_call_push_value(call, argument);
}

PocoStatus poco_call_push_double(PocoCall* call, double value)
{
	PocoCallbackValue argument = {POCO_CALLBACK_VALUE_DOUBLE, {.double_value = value}};

	return poco_call_push_value(call, argument);
}

static PocoStatus poco_call_reserve_pointer_registration(PocoCall* call)
{
	PocoCallPointerRegistration* grown;
	size_t capacity;

	if (call->registered_pointer_count < call->registered_pointer_capacity) {
		return POCO_STATUS_OK;
	}
	capacity = call->registered_pointer_capacity != 0 ? call->registered_pointer_capacity * 2 : 2;
	if (capacity < call->registered_pointer_capacity || capacity > SIZE_MAX / sizeof(*grown)) {
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	grown = realloc(call->pointer_registrations, capacity * sizeof(*grown));
	if (grown == NULL) {
		return POCO_STATUS_OUT_OF_MEMORY;
	}
	call->pointer_registrations = grown;
	call->registered_pointer_capacity = capacity;
	return POCO_STATUS_OK;
}

static void poco_call_release_pointer_registrations(PocoCall* call)
{
	PocoPointerRegistry* registry;

	if (call == NULL || call->activation == NULL) {
		return;
	}
	registry = call->activation->pointer_registry;
	while (call->registered_pointer_count != 0) {
		void* pointer = call->pointer_registrations[--call->registered_pointer_count].pointer;

		(void)poco_pointer_registry_unregister_borrowed(registry, pointer);
	}
}

PocoStatus poco_call_push_pointer(PocoCall* call, void* pointer, size_t byte_count,
								  PocoPointerPermission permissions)
{
	const uint32_t valid_permissions = POCO_POINTER_PERMISSION_READ | POCO_POINTER_PERMISSION_WRITE;
	PocoCallbackValue argument = {0};
	PocoPointerRegistry* registry;
	uint32_t permission_bits = (uint32_t)permissions;
	uintptr_t start;
	uintptr_t end;
	PocoStatus status;
	size_t index;
	int registered = 0;

	if (call == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	argument.kind = POCO_CALLBACK_VALUE_POPOT;
	if (call->invoked || call->activation == NULL || call->activation->program == NULL ||
		call->activation->program->vm == NULL || call->activation->program->vm->destroy_requested) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	start = (uintptr_t)pointer;
	if (pointer == NULL || byte_count == 0 || byte_count - 1 > UINTPTR_MAX - start ||
		(permission_bits & valid_permissions) == 0 || (permission_bits & ~valid_permissions) != 0) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	end = start + byte_count - 1;
	registry = call->activation->pointer_registry;
	for (index = 0; index < call->registered_pointer_count; ++index) {
		const PocoCallPointerRegistration* existing = &call->pointer_registrations[index];
		uintptr_t existing_start = (uintptr_t)existing->pointer;
		uintptr_t existing_end = existing_start + existing->byte_count - 1;

		if (existing_start == start) {
			if (existing->byte_count != byte_count || existing->permissions != permission_bits) {
				return POCO_STATUS_PARAMETER_RANGE;
			}
			break;
		}
		if (start <= existing_end && existing_start <= end &&
			existing->permissions != permission_bits) {
			return POCO_STATUS_PARAMETER_RANGE;
		}
	}
	if (index == call->registered_pointer_count) {
		status = poco_call_reserve_pointer_registration(call);
		if (status != POCO_STATUS_OK) {
			return status;
		}
		status = (PocoStatus)poco_pointer_registry_register_borrowed(registry, pointer, byte_count,
																	 permission_bits);
		if (status != POCO_STATUS_OK) {
			return status;
		}
		call->pointer_registrations[call->registered_pointer_count].pointer = pointer;
		call->pointer_registrations[call->registered_pointer_count].byte_count = byte_count;
		call->pointer_registrations[call->registered_pointer_count].permissions = permission_bits;
		++call->registered_pointer_count;
		registered = 1;
	}
	argument.value.popot_value.pt = pointer;
	argument.value.popot_value.min = pointer;
	argument.value.popot_value.max = (void*)end;
	status = poco_call_push_value(call, argument);
	if (status != POCO_STATUS_OK && registered) {
		--call->registered_pointer_count;
		(void)poco_pointer_registry_unregister_borrowed(registry, pointer);
	}
	return status;
}

static PocoStatus poco_call_coerce_value(PocoCallbackValue input, const Type_info* type,
										 PocoCallbackValue* output)
{
	if (type == NULL || output == NULL) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	*output = po_invalid_callback_value();
	switch (type->ido_type) {
		case IDO_INT:
			output->kind = POCO_CALLBACK_VALUE_INT;
			switch (input.kind) {
				case POCO_CALLBACK_VALUE_INT:
					output->value.int_value = input.value.int_value;
					return POCO_STATUS_OK;
				case POCO_CALLBACK_VALUE_LONG:
					output->value.int_value = (int)input.value.long_value;
					return POCO_STATUS_OK;
				case POCO_CALLBACK_VALUE_DOUBLE:
					if (!isfinite(input.value.double_value) ||
						(long double)input.value.double_value < (long double)INT_MIN ||
						(long double)input.value.double_value > (long double)INT_MAX) {
						return POCO_STATUS_PARAMETER_RANGE;
					}
					output->value.int_value = (int)input.value.double_value;
					return POCO_STATUS_OK;
				default:
					return POCO_STATUS_PARAMETER_RANGE;
			}
		case IDO_LONG:
			output->kind = POCO_CALLBACK_VALUE_LONG;
			switch (input.kind) {
				case POCO_CALLBACK_VALUE_INT:
					output->value.long_value = input.value.int_value;
					return POCO_STATUS_OK;
				case POCO_CALLBACK_VALUE_LONG:
					output->value.long_value = input.value.long_value;
					return POCO_STATUS_OK;
				case POCO_CALLBACK_VALUE_DOUBLE:
					if (!isfinite(input.value.double_value) ||
						(long double)input.value.double_value < (long double)LONG_MIN ||
						(long double)input.value.double_value > (long double)LONG_MAX) {
						return POCO_STATUS_PARAMETER_RANGE;
					}
					output->value.long_value = (long)input.value.double_value;
					return POCO_STATUS_OK;
				default:
					return POCO_STATUS_PARAMETER_RANGE;
			}
		case IDO_DOUBLE:
			output->kind = POCO_CALLBACK_VALUE_DOUBLE;
			switch (input.kind) {
				case POCO_CALLBACK_VALUE_INT:
					output->value.double_value = input.value.int_value;
					return POCO_STATUS_OK;
				case POCO_CALLBACK_VALUE_LONG:
					output->value.double_value = (double)input.value.long_value;
					return POCO_STATUS_OK;
				case POCO_CALLBACK_VALUE_DOUBLE:
					output->value.double_value = input.value.double_value;
					return POCO_STATUS_OK;
				default:
					return POCO_STATUS_PARAMETER_RANGE;
			}
		case IDO_POINTER:
			if (input.kind != POCO_CALLBACK_VALUE_POPOT) {
				return POCO_STATUS_PARAMETER_RANGE;
			}
			*output = input;
			return POCO_STATUS_OK;
		default:
			return POCO_STATUS_PARAMETER_RANGE;
	}
}

static PocoCallbackValue poco_call_result_value(const Type_info* type, Pt_num result)
{
	PocoCallbackValue value = po_invalid_callback_value();

	if (type == NULL) {
		return value;
	}
	switch (type->ido_type) {
		case IDO_INT:
			value.kind = POCO_CALLBACK_VALUE_INT;
			value.value.int_value = result.i;
			break;
		case IDO_LONG:
			value.kind = POCO_CALLBACK_VALUE_LONG;
			value.value.long_value = result.l;
			break;
		case IDO_DOUBLE:
			value.kind = POCO_CALLBACK_VALUE_DOUBLE;
			value.value.double_value = result.d;
			break;
		case IDO_POINTER:
			value.kind = POCO_CALLBACK_VALUE_POPOT;
			value.value.popot_value = result.ppt;
			break;
		default:
			break;
	}
	return value;
}

PocoStatus poco_call_invoke(PocoCall* call, PocoCallbackValue* out_result)
{
	PocoCallbackValue* coerced = NULL;
	const Symbol* parameter;
	PocoStatus status = POCO_STATUS_OK;
	Pt_num result = {0};
	long error_line = 0;
	long* previous_err_line;
	size_t index;

	if (out_result != NULL) {
		*out_result = po_invalid_callback_value();
	}
	if (call == NULL) {
		return POCO_STATUS_NULL_REFERENCE;
	}
	if (call->invoked) {
		return POCO_STATUS_PARAMETER_RANGE;
	}
	call->invoked = 1;
	if (call->activation == NULL || call->activation->program == NULL ||
		call->activation->program->vm == NULL || call->activation->program->vm->destroy_requested ||
		call->function == NULL || call->function->pcount < 0 ||
		call->value_count != (size_t)call->function->pcount) {
		status = POCO_STATUS_PARAMETER_RANGE;
		goto OUT;
	}
	if (call->value_count != 0) {
		coerced = malloc(call->value_count * sizeof(*coerced));
		if (coerced == NULL) {
			status = POCO_STATUS_OUT_OF_MEMORY;
			goto OUT;
		}
	}
	parameter = call->function->parameters;
	for (index = 0; index < call->value_count; ++index) {
		if (parameter == NULL) {
			status = POCO_STATUS_PARAMETER_RANGE;
			goto OUT;
		}
		status = poco_call_coerce_value(call->values[index], parameter->ti, &coerced[index]);
		if (status != POCO_STATUS_OK) {
			goto OUT;
		}
		parameter = parameter->link;
	}
	if (parameter != NULL) {
		status = POCO_STATUS_PARAMETER_RANGE;
		goto OUT;
	}
	previous_err_line = call->activation->err_line;
	call->activation->err_line = &error_line;
	status = (PocoStatus)po_activation_run_entry_values(call->activation, call->function->name,
														coerced, call->value_count, &result);
	call->activation->err_line = previous_err_line;
	if (status == Err_in_err_file && call->activation->builtin_error == Err_poco_ffi_bounds) {
		status = POCO_STATUS_FFI_BOUNDS;
	}
	po_activation_publish_last_error(call->activation);
	if (status != POCO_STATUS_OK) {
		po_vm_report(call->activation->program->vm, status, NULL, error_line, 0,
					 "Poco function call failed");
		goto OUT;
	}
	if (out_result != NULL) {
		*out_result = poco_call_result_value(call->function->return_type, result);
	}

OUT:
	poco_call_release_pointer_registrations(call);
	free(coerced);
	return status;
}

void poco_call_end(PocoCall* call)
{
	if (call == NULL) {
		return;
	}
	poco_call_release_pointer_registrations(call);
	free(call->pointer_registrations);
	free(call->values);
	free(call);
}
