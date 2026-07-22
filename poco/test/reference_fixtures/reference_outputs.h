#ifndef POCO_TEST_REFERENCE_OUTPUTS_H
#define POCO_TEST_REFERENCE_OUTPUTS_H

#include <stddef.h>
#include <stdint.h>

/*
 * Single source of truth for the deterministic output oracle used by the
 * phase-2 multi-program and phase-3 same-program concurrency gates.
 */
typedef struct PocoReferenceOutput {
	const char* script_name;
	int32_t expected_result;
} PocoReferenceOutput;

enum { POCO_REFERENCE_REPEAT_COUNT = 128, POCO_REFERENCE_REPEATED_RESULT = 155 };

#define POCO_REFERENCE_REPEATED_SCRIPT "repeated.poc"

static const PocoReferenceOutput poco_reference_outputs[] = {
	{"arithmetic.poc", 17},
	{"factorial.poc", 720},
	{"fibonacci.poc", 55},
};

static const size_t poco_reference_output_count =
	sizeof(poco_reference_outputs) / sizeof(poco_reference_outputs[0]);

#endif /* POCO_TEST_REFERENCE_OUTPUTS_H */
