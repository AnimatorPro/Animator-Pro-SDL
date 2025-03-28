cmake_minimum_required(VERSION 3.26 FATAL_ERROR)

project(animator-pro)

cmake_policy(SET CMP0114 NEW)

if(APPLE)
	## fixes issues with nativefiledialogextended
	enable_language(OBJC)
endif()

if (PLATFORM STREQUAL "OS64")
	## iOS-specific
	message("Building for platform iOS.")
endif()

if (CMAKE_GENERATOR STREQUAL "Xcode")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D IS_BUNDLE")
endif()

## all build options
option(WITH_ANI  "Build Animator Pro." ON)
option(WITH_POCO "Build with the Poco scripting language." OFF)
option(USE_ASAN  "Use Clang Memory Address Sanitizer"      OFF)


if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	# will not take effect without FORCE
	set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/bin" CACHE PATH "Install top-level directory" FORCE)
endif()

if (UNIX)
	if (USE_ASAN)
		set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -fsanitize=address -g")
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g")
	endif()

	add_compile_options(
		-Wno-deprecated-declarations
		-Wno-incompatible-pointer-types
		-Wno-address-of-packed-member
		-Wno-sometimes-uninitialized
		-Wno-format-security
		-Wno-null-pointer-subtraction
		-Wdeprecated-non-prototype
	)
endif()

## thirdparty dependencies
option(SDL_STATIC "" ON)
option(SDL_SHARED "" OFF)

add_subdirectory(thirdparty/sdl3)
include_directories(thirdparty/sdl3/include)
add_subdirectory(thirdparty/nativefiledialogextended)
include_directories(thirdparty/nativefiledialogextended/src/include)


if (WITH_POCO)
	add_compile_definitions(-DWITH_POCO=1)
	add_subdirectory(thirdparty/libffi)
	add_subdirectory(thirdparty/hashmap)
	add_subdirectory(thirdparty/imgui/imgui)
	add_subdirectory(thirdparty/imgui/ImGuiColorTextEdit)
	include_directories(thirdparty/imgui/imgui)
	include_directories(thirdparty/imgui/imgui/backends)
	include_directories(thirdparty/imgui/imgui_club/imgui_memory_editor)
	include_directories(thirdparty/imgui/ImGuiColorTextEdit)
endif()


## main
add_subdirectory(src)

## tests
if (APPLE)
	add_subdirectory(tests)
endif()
