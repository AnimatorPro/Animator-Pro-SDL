
cmake_minimum_required(VERSION 3.16)
project(TestApp)

set(CMAKE_C_STANDARD 11)

# set(SDL_TEST_SOURCES
# 		sdl_test.c
# 		../src/pj_sdl/pj_sdl.c
# )

# add_executable(sdl_test "${SDL_TEST_SOURCES}")
# target_include_directories(sdl_test PUBLIC ../src/inc)
# target_link_libraries(sdl_test
# 		SDL3::SDL3-static
# 		m
# 		nfd
# 		"-framework AppKit"
# 		"-framework UniformTypeIdentifiers"
# )

# set_target_properties(
# 		sdl_test
# 	PROPERTIES
# 		MACOSX_BUNDLE YES
# 		XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
# 		XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.vpaint.test-app"
# )

# ## pre-build required or signing will fail
# add_custom_command(
# 	TARGET sdl_test
# 	PRE_BUILD
# 		COMMAND ${CMAKE_COMMAND} -E copy_directory
# 			"${CMAKE_CURRENT_SOURCE_DIR}/resource" "$<TARGET_FILE_DIR:sdl_test>/../Resources"
# )


## ffi test
if (WITH_POCO)
	set(FFI_TEST_SOURCES
            ffi_test.cc
    )

	add_executable(ffi_test "${FFI_TEST_SOURCES}")
	target_link_libraries(ffi_test ffi_static)
endif()
