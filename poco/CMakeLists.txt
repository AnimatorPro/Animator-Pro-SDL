cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(Poco VERSION 2.0.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# An embedding host that does add_subdirectory(poco) has no use for Poco's own
# test tree: it brings ~50 extra targets, ~150 ctest registrations, a hard
# Threads dependency and CTest's Experimental/Nightly machinery.  Build the
# suite only when Poco is the top-level project, or when a host asks for it.
if(DEFINED PROJECT_IS_TOP_LEVEL)
    set(_poco_is_top_level ${PROJECT_IS_TOP_LEVEL})
elseif(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_poco_is_top_level ON)
else()
    set(_poco_is_top_level OFF)
endif()
option(POCO_BUILD_TESTS "Build the Poco test suite" ${_poco_is_top_level})

# The `poco` command-line interpreter is Poco's own deliverable, not part of
# the embedding contract.  A host that does add_subdirectory(poco) and then
# `cmake --install` should not find a stray interpreter in its install tree,
# so ship it only when Poco is the top-level project or a host asks for it.
option(POCO_INSTALL_CLI "Install the poco command-line interpreter" ${_poco_is_top_level})
unset(_poco_is_top_level)

# This end-to-end gate copies, builds, installs, and tests Poco independently,
# so keep it opt-in for normal project test runs.
option(POCO_ENABLE_EXTRACTION_BOUNDARY_TEST
    "Enable the standalone Poco extraction-boundary CTest"
    OFF
)

if(POCO_BUILD_TESTS)
    include(CTest)
endif()

# Static linkage is the primary Apple embedding deliverable, so it is the
# default.  A shared library remains available for hosts that prefer to load
# Poco at runtime.  This is a dedicated switch rather than BUILD_SHARED_LIBS so
# an embedding project's global choice does not silently flip Poco's linkage.
option(POCO_BUILD_SHARED "Build poco_core as a shared library instead of static" OFF)

# The examples are demonstrations, not part of the library.  They stay off by
# default for every caller, including a host that already defines SDL3::SDL3 --
# having SDL3 around is not a request for an extra executable and its install
# rules.  A host that wants them sets POCO_BUILD_EXAMPLES before adding Poco.
option(POCO_BUILD_EXAMPLES "Build Poco examples that require SDL3" OFF)

# The 1991 String type experiment.  It is off by default -- it was never a
# production feature and nothing in the shipping language depends on it -- but
# it is retained deliberately, not deleted, and the option is what keeps it
# selectable.  A configuration with this ON is compiled by the test suite so
# the branch cannot silently rot again.
option(POCO_STRING_EXPERIMENT "Compile the experimental Poco String type" OFF)

# The parent Animator build uses directory-wide include paths.  Clear the
# inherited property before creating Poco targets so standalone and embedded
# builds see the same headers.
set_property(DIRECTORY PROPERTY INCLUDE_DIRECTORIES "")

# Poco owns every target it needs.  The dependencies use Poco-private names
# and are never installed or exported to embedding projects.
add_subdirectory(third_party/libffi)
add_subdirectory(third_party/hashmap)
add_subdirectory(third_party/blake3)

set(POCO_CORE_SOURCES
	activation.c
    bcutil.c
    bytecode_container.c
    bop.c
    chopper.c
    code.c
	declare.c
	debug.c
    path_operations.c
    fold.c
    funccall.c
    mathlib.c
    pocmemry.c
    poco.c
    port.c
    pocodis.c
    pocoface.c
    pocolib.c
    pocoload.c
    pocoop.c
    pocotype.c
    postring.c
    poco_ffi.c
    poco_hash.c
    program_image.c
    pp.c
    ppeval.c
    runops.c
    safefile.c
	serialization.c
    statemen.c
    standard_library.c
    strlib.c
    struct.c
    token.c
    trace.c
    varinit.c
)
list(TRANSFORM POCO_CORE_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/src/")

# The distribution is one library.  libffi and hashmap are linked into it
# privately, leaving a consumer exactly one CMake target: Poco::poco.  The
# linkage follows POCO_BUILD_SHARED (static by default).
if(POCO_BUILD_SHARED)
    set(_poco_core_linkage SHARED)
else()
    set(_poco_core_linkage STATIC)
endif()
add_library(poco_core ${_poco_core_linkage} ${POCO_CORE_SOURCES})
add_library(Poco::poco ALIAS poco_core)
set_target_properties(poco_core PROPERTIES
    OUTPUT_NAME poco
    EXPORT_NAME poco
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    # Poco's retained hashmap integration uses GNU typeof() macros.  Keep
    # that compatibility local to this target instead of setting a directory
    # or parent-wide C standard.
    C_EXTENSIONS YES
    # Keep the static archive linkable into shared hosts (frameworks, plugins).
    POSITION_INDEPENDENT_CODE ON
)
target_include_directories(poco_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
# Wrap the bundled dependency targets in BUILD_INTERFACE.  In the build tree
# (add_subdirectory embedding) they still link transitively, which a static
# poco_core needs.  In the install interface they are dropped: a shared
# poco_core bakes them into libpoco, and install(EXPORT) would otherwise reject
# poco_ffi/poco_hashmap because they are private and never exported.
target_link_libraries(poco_core PRIVATE
    $<BUILD_INTERFACE:poco_ffi>
    $<BUILD_INTERFACE:poco_hashmap>
    $<BUILD_INTERFACE:poco_blake3>
)
if(CMAKE_DL_LIBS)
    target_link_libraries(poco_core PRIVATE "${CMAKE_DL_LIBS}")
endif()
if(CMAKE_BUILD_TYPE MATCHES "^[Dd]ebug$")
    target_compile_definitions(poco_core PRIVATE DEVELOPMENT=1 DEBUG=1)
endif()
# PUBLIC: poco.h is compiled by the CLI and by embedding hosts that reach for
# the private compiler headers, and the String type changes struct layouts, so
# every translation unit in a build has to agree on it.
if(POCO_STRING_EXPERIMENT)
    target_compile_definitions(poco_core PUBLIC STRING_EXPERIMENT=1)
endif()

set(POCO_CLI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cli_debugger.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/pocodos_standalone.c"
)
if(APPLE OR UNIX)
    list(APPEND POCO_CLI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/poco_unix.c")
endif()
# The target is poco_cli, not poco: `poco` is a name an embedding host is
# entitled to use for its own target, and add_subdirectory() would collide with
# it.  The produced binary is still named poco.
add_executable(poco_cli ${POCO_CLI_SOURCES})
# The CLI compiles legacy private compiler headers, so it needs libffi's build
# include path while compiling.  This remains an internal relationship; an
# embedding project links only Poco::poco.
target_link_libraries(poco_cli PRIVATE Poco::poco poco_ffi)
set_target_properties(poco_cli PROPERTIES
    OUTPUT_NAME poco
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS YES
)
if(APPLE)
    set_target_properties(poco_cli PROPERTIES INSTALL_RPATH "@loader_path/lib")
elseif(UNIX)
    set_target_properties(poco_cli PROPERTIES INSTALL_RPATH "$ORIGIN/lib")
endif()

set(POCO_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/Poco")
# A shared poco_core absorbs libffi/hashmap/blake3 at link time, so the install
# is one file.  A static one cannot: libpoco.a would reach the consumer with
# undefined ffi_*/hashmap_*/blake3_* symbols.  Install those archives into a
# private subdirectory of libdir and let PocoConfig append them behind
# Poco::poco, so the consumer still names exactly one target.
set(POCO_PRIVATE_LIBDIR "${CMAKE_INSTALL_LIBDIR}/poco")
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PocoConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PocoConfig.cmake"
    INSTALL_DESTINATION "${POCO_CMAKE_INSTALL_DIR}"
    PATH_VARS POCO_PRIVATE_LIBDIR
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/PocoConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(TARGETS poco_core
    EXPORT PocoTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
if(NOT POCO_BUILD_SHARED)
    install(TARGETS poco_hashmap poco_blake3
        ARCHIVE DESTINATION "${POCO_PRIVATE_LIBDIR}"
    )
    if(NOT POCO_FFI_STATIC_LIBRARY)
        message(FATAL_ERROR
            "A static Poco install needs the private libffi archive, but "
            "POCO_FFI_STATIC_LIBRARY was not set by third_party/libffi.")
    endif()
    install(FILES "${POCO_FFI_STATIC_LIBRARY}"
        DESTINATION "${POCO_PRIVATE_LIBDIR}"
        RENAME "${CMAKE_STATIC_LIBRARY_PREFIX}poco_ffi${CMAKE_STATIC_LIBRARY_SUFFIX}"
    )
endif()
if(POCO_INSTALL_CLI)
    install(TARGETS poco_cli RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/poco/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/poco"
    FILES_MATCHING PATTERN "*.h"
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/PocoConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/PocoConfigVersion.cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PocoModule.cmake"
    DESTINATION "${POCO_CMAKE_INSTALL_DIR}"
)
install(EXPORT PocoTargets
    FILE PocoTargets.cmake
    NAMESPACE Poco::
    DESTINATION "${POCO_CMAKE_INSTALL_DIR}"
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
if(POCO_BUILD_EXAMPLES)
    add_subdirectory(examples/snake)
endif()
# hello is the canonical generic-module example and doubles as the module-ABI
# smoke test, so it is built for a test-only configure as well.  Unlike snake
# it needs nothing but Poco itself.
if(POCO_BUILD_EXAMPLES OR POCO_BUILD_TESTS)
    add_subdirectory(examples/hello)
endif()
if(POCO_BUILD_TESTS)
    add_subdirectory(test)
endif()
