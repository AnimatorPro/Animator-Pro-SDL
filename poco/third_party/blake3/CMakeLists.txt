# Poco-private BLAKE3 build. The vendored source remains an implementation
# detail of poco_core and is not installed or exported as a separate package.
add_library(poco_blake3 STATIC
    blake3.c
    blake3_dispatch.c
    blake3_portable.c
)
set_target_properties(poco_blake3 PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE YES
)
target_include_directories(poco_blake3 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# The reference dispatch layer uses the four-way NEON implementation for
# hash_many. Apple Silicon guarantees NEON, so no runtime feature probe is
# needed. Other targets retain the portable implementation in this focused
# vendor integration.
if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64|AARCH64)$")
    target_sources(poco_blake3 PRIVATE blake3_neon.c)
    target_compile_definitions(poco_blake3 PRIVATE
        BLAKE3_USE_NEON=1
        BLAKE3_NO_SSE2
        BLAKE3_NO_SSE41
        BLAKE3_NO_AVX2
        BLAKE3_NO_AVX512
    )
    set(POCO_BLAKE3_HAS_NEON TRUE CACHE INTERNAL
        "Poco BLAKE3 was built with Apple ARM NEON")
else()
    target_compile_definitions(poco_blake3 PRIVATE
        BLAKE3_USE_NEON=0
        BLAKE3_NO_SSE2
        BLAKE3_NO_SSE41
        BLAKE3_NO_AVX2
        BLAKE3_NO_AVX512
    )
    set(POCO_BLAKE3_HAS_NEON FALSE CACHE INTERNAL
        "Poco BLAKE3 was built with Apple ARM NEON")
endif()
