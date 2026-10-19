# PCRE2 10.49 (pcre2-10.49.tar.gz, sha256 929f0b20e62879252a15886b06c89f1edef61a363cbd5826fb041080a5e557ae),
# the library files only, built as the 16-bit library with Unicode support. LICENCE.md and
# deps/sljit/LICENSE are the licences. src/pcre2.h, src/config.h and src/pcre2_chartables.c are
# the release's pcre2.h.generic, config.h.generic and pcre2_chartables.c.dist.
option(HDTRANS_PCRE2_JIT "Build the PCRE2 JIT compiler (not for WebAssembly)" OFF)

set(PCRE2_SOURCES
    auto_possess chartables chkdint compile compile_cgroup compile_class config context convert
    dfa_match error extuni find_bracket jit_compile maketables match match_data match_next newline
    ord2utf pattern_info script_run serialize string_utils study substitute substring tables ucd
    valid_utf xclass)
list(TRANSFORM PCRE2_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/src/pcre2_")
list(TRANSFORM PCRE2_SOURCES APPEND ".c")

# Upstream code: none of the project's warning or C++ flags.
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")
if(MSVC)
    string(REGEX REPLACE "/W[0-4]|/WX" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
endif()

add_library(pcre2-16 STATIC ${PCRE2_SOURCES})
set_target_properties(pcre2-16 PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(pcre2-16 PUBLIC src)
target_compile_definitions(pcre2-16
    PUBLIC PCRE2_CODE_UNIT_WIDTH=16 PCRE2_STATIC
    PRIVATE HAVE_CONFIG_H SUPPORT_UNICODE HAVE_ASSERT_H HAVE_LIMITS_H HAVE_STDINT_H HAVE_INTTYPES_H
            HAVE_STRING_H HAVE_STDLIB_H)
if(HDTRANS_PCRE2_JIT AND NOT EMSCRIPTEN)
    target_compile_definitions(pcre2-16 PRIVATE SUPPORT_JIT)
endif()
if(MSVC)
    target_compile_options(pcre2-16 PRIVATE /w)
else()
    target_compile_options(pcre2-16 PRIVATE -w)
endif()
