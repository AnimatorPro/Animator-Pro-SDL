# Poco-private libffi wrapper.  libffi is built out of tree so a standalone
# Poco configure never writes generated files or archives into its source tree.
include(ExternalProject)

set(_poco_ffi_artifacts "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
set(_poco_ffi_configure_options
    --disable-shared
    --disable-docs
    --disable-multi-os-directory
)
if(CMAKE_BUILD_TYPE MATCHES "^[Dd]ebug$")
    list(APPEND _poco_ffi_configure_options --enable-debug)
endif()

set(_poco_ffi_environment "CC=${CMAKE_C_COMPILER}")
if(NOT WIN32)
    # Poco is deliberately a bundled shared library, so the static libffi
    # objects must be suitable for inclusion in it on ELF platforms.
    list(APPEND _poco_ffi_environment "CFLAGS=-fPIC")
endif()

ExternalProject_Add(poco_libffi_build
    PREFIX "${CMAKE_CURRENT_BINARY_DIR}/build"
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/main"
    BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/build/work"
    INSTALL_DIR "${_poco_ffi_artifacts}"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    PATCH_COMMAND ""
    CONFIGURE_COMMAND
        "${CMAKE_COMMAND}" -E env ${_poco_ffi_environment}
        "<SOURCE_DIR>/configure"
        "--prefix=<INSTALL_DIR>"
        ${_poco_ffi_configure_options}
    BUILD_COMMAND make
    INSTALL_COMMAND make install
    BUILD_BYPRODUCTS "${_poco_ffi_artifacts}/lib/libffi.a"
)

# This target is an implementation detail.  Its prefix deliberately keeps it
# out of Poco's public target namespace and it is never installed or exported.
add_library(poco_ffi INTERFACE)
add_dependencies(poco_ffi poco_libffi_build)
target_include_directories(poco_ffi INTERFACE
    "${_poco_ffi_artifacts}/include"
)
target_link_libraries(poco_ffi INTERFACE
    "${_poco_ffi_artifacts}/lib/libffi.a"
)
