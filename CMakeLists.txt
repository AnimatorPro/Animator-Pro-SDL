cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

project(animator-pro)

cmake_policy(SET CMP0114 NEW)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable testing
enable_testing()

if(APPLE)
	enable_language(OBJC)
	add_compile_definitions(-DSDL_PLATFORM_APPLE=1)
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
	)
endif()

if (APPLE)
	add_compile_options(
		-Wdeprecated-non-prototype
	)
endif()

## thirdparty dependencies
option(SDL_STATIC "" ON)
option(SDL_SHARED "" OFF)
option(BUILD_SHARED_LIBS "" OFF)

set(SDLIMAGE_VENDORED ON CACHE BOOL "" FORCE)
set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_WEBP OFF CACHE BOOL "" FORCE)

add_subdirectory(thirdparty/sdl3)
include_directories(thirdparty/sdl3/include)
add_subdirectory(thirdparty/sdl3_image)
include_directories(thirdparty/sdl3_image/include)
add_subdirectory(thirdparty/miniz)
include_directories(thirdparty/miniz
)
add_subdirectory(thirdparty/giflib)
include_directories(thirdparty/giflib)
include_directories(thirdparty/stb)


if (WITH_POCO)
	add_compile_definitions(-DWITH_POCO=1)
	add_subdirectory(thirdparty/imgui/imgui)
	include_directories(thirdparty/imgui/imgui)
	include_directories(thirdparty/imgui/imgui/backends)
	include_directories(thirdparty/imgui/imgui_club/imgui_memory_editor)

	# Build animhost first (needed by POE modules)
	add_subdirectory(src/animhost)
	
	# Add new standalone poco subproject
	add_subdirectory(poco)

	##!TODO: Fix this extension
	# add_subdirectory(thirdparty/imgui/ImGuiColorTextEdit)
	# include_directories(thirdparty/imgui/ImGuiColorTextEdit)
endif()


## main
add_subdirectory(src)

## POE modules (must come AFTER src so native .poe installs are not
## overwritten by the src/resource directory copy)
if (WITH_POCO)
	add_subdirectory(poco/poekit)
endif()

## tests
if (APPLE)
	add_subdirectory(tests)
endif()
