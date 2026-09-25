# A malformed '#pragma poco native' argument must reach the user as the
# native_arg_bad diagnostic and an ordinary non-zero exit.  This ran as a crash
# instead: the fatal is raised while the tokenizer is already reading past a
# preceding declaration, and the declaration parser went on to use a symbol it
# had never been given.  A signal here is indistinguishable from a compiler bug
# from the outside, so the exit status is asserted as strictly as the message.

foreach(required_variable POCO_EXECUTABLE POCO_WORK_DIR)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} must be set")
    endif()
endforeach()

file(MAKE_DIRECTORY "${POCO_WORK_DIR}")

set(BAD_ARGUMENT_MESSAGE "expects 'begin' or 'end'")

# Compile one source and hand back the exit status and both streams.  A signal
# makes execute_process report a description rather than a number, which is the
# distinction this test exists to make.
function(compile_source name source)
    set(path "${POCO_WORK_DIR}/${name}.poc")
    file(WRITE "${path}" "${source}")
    execute_process(
        COMMAND "${POCO_EXECUTABLE}" -c "${path}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout_text
        ERROR_VARIABLE stderr_text
    )
    set(poco_result "${result}" PARENT_SCOPE)
    set(poco_output "${stdout_text}${stderr_text}" PARENT_SCOPE)
endfunction()

function(expect_diagnosed name source)
    compile_source("${name}" "${source}")
    if(NOT poco_result MATCHES "^-?[0-9]+$")
        message(FATAL_ERROR "${name} did not exit normally (${poco_result})\n${poco_output}")
    endif()
    if(poco_result EQUAL 0)
        message(FATAL_ERROR "${name} compiled\n${poco_output}")
    endif()
    if(NOT poco_output MATCHES "${BAD_ARGUMENT_MESSAGE}")
        message(FATAL_ERROR "${name} was not diagnosed as a bad native argument\n${poco_output}")
    endif()
endfunction()

function(expect_accepted name source)
    compile_source("${name}" "${source}")
    if(NOT poco_result EQUAL 0)
        message(FATAL_ERROR "${name} was refused (${poco_result})\n${poco_output}")
    endif()
endfunction()

expect_diagnosed(native_bogus [[
#pragma poco native bogus
main() { return 0; }
]])

expect_diagnosed(native_missing [[
#pragma poco native
main() { return 0; }
]])

expect_diagnosed(native_begin_trailing [[
#pragma poco native begin extra
int host_add(int a, int b);
#pragma poco native end
main() { return 0; }
]])

expect_diagnosed(native_end_trailing [[
#pragma poco native begin
int host_add(int a, int b);
#pragma poco native end extra
main() { return 0; }
]])

# The same spellings behind a declaration the tokenizer is mid-way through when
# the fatal lands.  This is the arrangement that used to take the process down.
expect_diagnosed(native_bogus_after_typedef [[
typedef int i32;
#pragma poco native bogus
main() { return 0; }
]])

expect_diagnosed(native_missing_after_typedef [[
typedef int i32;
#pragma poco native
main() { return 0; }
]])

expect_diagnosed(native_trailing_after_typedef [[
typedef int i32;
#pragma poco native begin extra
int host_add(int a, int b);
#pragma poco native end
main() { return 0; }
]])

expect_diagnosed(native_bogus_after_struct [[
struct point { int x; int y; };
#pragma poco native bogus
main() { return 0; }
]])

# Well formed regions are untouched, including the two things that look like
# trailing tokens but are not.
expect_accepted(native_well_formed [[
typedef int i32;
#pragma poco native begin
int host_add(int a, int b);
#pragma poco native end
main() { i32 n; n = 0; return n; }
]])

expect_accepted(native_semicolon [[
#pragma poco native begin ;
int host_add(int a, int b);
#pragma poco native end ;
main() { return 0; }
]])

expect_accepted(native_comment [[
#pragma poco native begin /* open */
int host_add(int a, int b);
#pragma poco native end /* close */
main() { return 0; }
]])
