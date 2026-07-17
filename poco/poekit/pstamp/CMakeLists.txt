cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

# This module consumes Animator's raster and graphics APIs through the
# explicitly Animator-only helper.
include("${CMAKE_SOURCE_DIR}/src/ani_poco/AniPocoPoeLibrary.cmake")

# Used by flisumry.poc and installed in Animator's resource directory.
ani_add_poe_library(pstamp
    SOURCES pstamp.c
    DEPS trdutil
    LEGACY_POE
)
