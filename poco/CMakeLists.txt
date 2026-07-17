cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(Poco VERSION 1.0.0 LANGUAGES C)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# This end-to-end gate copies, builds, installs, and tests Poco independently,
# so keep it opt-in for normal project test runs.
option(POCO_ENABLE_EXTRACTION_BOUNDARY_TEST
    "Enable the standalone Poco extraction-boundary CTest"
    OFF
)

# The parent Animator build uses directory-wide include paths.  Clear the
# inherited property before creating Poco targets so standalone and embedded
# builds see the same headers.
set_property(DIRECTORY PROPERTY INCLUDE_DIRECTORIES "")

# Poco owns every target it needs.  The dependencies use Poco-private names
# and are never installed or exported to embedding projects.
add_subdirectory(third_party/libffi)
add_subdirectory(third_party/hashmap)

set(POCO_CORE_SOURCES
    bcutil.c
    bop.c
    chopper.c
    code.c
    declare.c
    pocodos_standalone.c
    fold.c
    funccall.c
    linklist.c
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
    pp.c
    ppeval.c
    runops.c
    safefile.c
    statemen.c
    standard_library.c
    strlib.c
    struct.c
    token.c
    trace.c
    varinit.c
)
list(TRANSFORM POCO_CORE_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/src/")

# The distribution is one shared library.  libffi and hashmap are linked into
# it privately, leaving a consumer exactly one CMake target: Poco::poco.
add_library(poco_core SHARED ${POCO_CORE_SOURCES})
add_library(Poco::poco ALIAS poco_core)
set_target_properties(poco_core PROPERTIES
    OUTPUT_NAME poco
    EXPORT_NAME poco
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    # Poco's retained hashmap integration uses GNU typeof() macros.  Keep
    # that compatibility local to this target instead of setting a directory
    # or parent-wide C standard.
    C_EXTENSIONS YES
)
target_include_directories(poco_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(poco_core PRIVATE poco_ffi poco_hashmap)
if(CMAKE_DL_LIBS)
    target_link_libraries(poco_core PRIVATE "${CMAKE_DL_LIBS}")
endif()
if(CMAKE_BUILD_TYPE MATCHES "^[Dd]ebug$")
    target_compile_definitions(poco_core PRIVATE DEVELOPMENT=1 DEBUG=1)
endif()

set(POCO_CLI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.c")
if(APPLE OR UNIX)
    list(APPEND POCO_CLI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/poco_unix.c")
endif()
add_executable(poco ${POCO_CLI_SOURCES})
# The CLI compiles legacy private compiler headers, so it needs libffi's build
# include path while compiling.  This remains an internal relationship; an
# embedding project links only Poco::poco.
target_link_libraries(poco PRIVATE Poco::poco poco_ffi)
set_target_properties(poco PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS YES
)
if(APPLE)
    set_target_properties(poco PROPERTIES INSTALL_RPATH "@loader_path/lib")
elseif(UNIX)
    set_target_properties(poco PROPERTIES INSTALL_RPATH "$ORIGIN/lib")
endif()

set(POCO_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/Poco")
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PocoConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PocoConfig.cmake"
    INSTALL_DESTINATION "${POCO_CMAKE_INSTALL_DIR}"
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
install(TARGETS poco RUNTIME DESTINATION .)
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

# poekit (legacy POE modules) is added only by the parent Animator project.
# It is not part of add_subdirectory(poco)'s public API.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
add_subdirectory(test)
