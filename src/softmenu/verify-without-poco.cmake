file(READ "${SOURCE_DIR}/src/CMakeLists.txt" animator_cmake)
string(REGEX MATCH "poco/src/(token|bcutil)\\.c|include_directories[^\\n\\r]*poco/include"
    animator_poco_dependency "${animator_cmake}")
if(animator_poco_dependency)
    message(FATAL_ERROR "Animator source still has a Poco tokenizer dependency: ${animator_poco_dependency}")
endif()

file(READ "${SOURCE_DIR}/src/softmenu/CMakeLists.txt" softmenu_cmake)
string(FIND "${softmenu_cmake}" "Poco::poco" softmenu_poco_link)
if(NOT softmenu_poco_link EQUAL -1)
    message(FATAL_ERROR "Softmenu still links Poco::poco")
endif()

file(READ "${SOURCE_DIR}/src/inc/token.h" token_header)
string(REGEX MATCH "#[ \\t]*include[^\\n\\r]*poco/" token_header_poco_include "${token_header}")
if(token_header_poco_include)
    message(FATAL_ERROR "Animator token header still includes Poco: ${token_header_poco_include}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${BINARY_DIR}" -G "${TEST_GENERATOR}"
        -DWITH_ANI=ON -DWITH_POCO=OFF
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_output
    ERROR_VARIABLE configure_error
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "Animator without Poco did not configure:\n${configure_output}\n${configure_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${BINARY_DIR}" --target ani
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "Animator without Poco did not build:\n${build_output}\n${build_error}")
endif()
