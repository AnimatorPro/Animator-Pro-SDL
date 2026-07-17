# Verify that compiling a script whose #pragma poco library names a missing
# POE module reports a compile error (non-zero exit) instead of crashing.
#
# Inputs: HARNESS (ani_poco_registration executable), SCRIPT (the .poc file).

if(NOT DEFINED HARNESS OR NOT DEFINED SCRIPT)
	message(FATAL_ERROR "HARNESS and SCRIPT are required")
endif()

execute_process(
	COMMAND "${HARNESS}" "${SCRIPT}"
	RESULT_VARIABLE result
	OUTPUT_VARIABLE stdout
	ERROR_VARIABLE stderr)

# A graceful compile failure exits non-zero with a normal code (1-127).  A
# crash surfaces as a non-numeric result ("Segmentation fault ...") or a
# signal-derived code (>= 128); exit 0 would mean the missing module was
# wrongly accepted.  All three must fail this test.
if(NOT result MATCHES "^[0-9]+$")
	message(FATAL_ERROR
		"harness crashed instead of failing gracefully (result '${result}').\n"
		"stdout:\n${stdout}\nstderr:\n${stderr}")
endif()
if(result EQUAL 0)
	message(FATAL_ERROR
		"compiling a script with a missing POE module must fail, but it exited 0.\n"
		"stdout:\n${stdout}\nstderr:\n${stderr}")
endif()
if(result GREATER_EQUAL 128)
	message(FATAL_ERROR
		"harness was killed by a signal (code ${result}) instead of failing gracefully.\n"
		"stdout:\n${stdout}\nstderr:\n${stderr}")
endif()

if(NOT "${stderr}${stdout}" MATCHES "not found in search paths")
	message(FATAL_ERROR
		"expected a 'not found in search paths' diagnostic.\n"
		"stdout:\n${stdout}\nstderr:\n${stderr}")
endif()

message(STATUS "missing POE module reported a compile error (exit ${result})")
