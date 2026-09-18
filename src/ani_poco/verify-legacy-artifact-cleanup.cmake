cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

foreach(required_variable POCO_SOURCE_DIR ANIMATOR_SOURCE_DIR)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

set(LEGACY_ARTIFACTS
    "${ANIMATOR_SOURCE_DIR}/src/rexlib/rexhost/REXENTRY.I"
    "${ANIMATOR_SOURCE_DIR}/src/rexlib/rexhost/MAKEFILE"
    "${ANIMATOR_SOURCE_DIR}/src/poekit/FORMIKE.INC"
    "${ANIMATOR_SOURCE_DIR}/src/poekit/INCFILES.INC"
    "${ANIMATOR_SOURCE_DIR}/src/poekit/KITFILES.INC"
    "${ANIMATOR_SOURCE_DIR}/src/poekit/KIT_OBJS.INC"
    "${ANIMATOR_SOURCE_DIR}/src/poekit/LIBFILES.INC"
    "${ANIMATOR_SOURCE_DIR}/src/poekit/MAKE.BAT"
    "${ANIMATOR_SOURCE_DIR}/src/poekit/MAKEKIT.BAT"
    "${ANIMATOR_SOURCE_DIR}/src/poekit/TESTMAKE.BAT"
    "${ANIMATOR_SOURCE_DIR}/src/poekit/VERSION.TXT"
)

foreach(artifact IN LISTS LEGACY_ARTIFACTS)
    if(EXISTS "${artifact}")
        message(FATAL_ERROR "Obsolete legacy build artifact remains: ${artifact}")
    endif()
endforeach()

# Modern build and packaging scripts must not bring back an explicit
# dependency on the retired Rex or Poekit artifacts.  Do not inspect source
# examples: rexentry.asm is retained historical source but is not a CMake
# input, whereas this gate is specifically about supported build paths.
file(GLOB_RECURSE BUILD_METADATA LIST_DIRECTORIES FALSE
    "${ANIMATOR_SOURCE_DIR}/CMakeLists.txt"
    "${ANIMATOR_SOURCE_DIR}/*.cmake"
    "${ANIMATOR_SOURCE_DIR}/*.sh"
    "${ANIMATOR_SOURCE_DIR}/*.bat"
)
foreach(metadata_file IN LISTS BUILD_METADATA)
    # CMakeFiles/ holds CMake's own generated bookkeeping, including the
    # TryCompile scratch projects other tests create and delete while this one
    # runs.  Globbing them in made this test fail at random under `ctest -j`,
    # and they are not build metadata anyone authored anyway.
    if(metadata_file MATCHES "/thirdparty/" OR
       metadata_file MATCHES "/CMakeFiles/" OR
       metadata_file STREQUAL "${CMAKE_CURRENT_LIST_FILE}")
        continue()
    endif()

    # The glob is a snapshot; a concurrently generated file can be gone by now.
    if(NOT EXISTS "${metadata_file}")
        continue()
    endif()

    file(READ "${metadata_file}" metadata)
    string(TOLOWER "${metadata}" metadata)
    string(REPLACE "\\" "/" metadata "${metadata}")
    foreach(reference
            "rexhost/rexentry.i"
            "rexhost/makefile"
            "poekit/formike.inc"
            "poekit/incfiles.inc"
            "poekit/kitfiles.inc"
            "poekit/kit_objs.inc"
            "poekit/libfiles.inc"
            "poekit/make.bat"
            "poekit/makekit.bat"
            "poekit/testmake.bat"
            "poekit/version.txt")
        string(FIND "${metadata}" "${reference}" reference_index)
        if(NOT reference_index EQUAL -1)
            message(FATAL_ERROR
                "Modern build metadata ${metadata_file} refers to retired artifact ${reference}")
        endif()
    endforeach()
endforeach()

file(READ "${POCO_SOURCE_DIR}/README.md" poco_readme)
foreach(required_text
        "poco_add_module()"
        "add_poe_library()"
        "ani_add_poe_library()"
        "only supported module build paths")
    string(FIND "${poco_readme}" "${required_text}" required_text_index)
    if(required_text_index EQUAL -1)
        message(FATAL_ERROR
            "poco/README.md must document ${required_text} for current module builds")
    endif()
endforeach()
