# libyaml 0.2.5 (yaml-0.2.5.tar.gz, sha256 c642ae9b75fee120b2d96c712538bd2cf283228d2337df2cf2988e3c02678ef4),
# library sources and headers only. License is the MIT licence.

# Upstream code: none of the project's warning or C++ flags.
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")
if(MSVC)
    string(REGEX REPLACE "/W[0-4]|/WX" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
endif()

add_library(yaml STATIC
    src/api.c src/dumper.c src/emitter.c src/loader.c
    src/parser.c src/reader.c src/scanner.c src/writer.c)
set_target_properties(yaml PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(yaml PUBLIC include)
target_compile_definitions(yaml
    PUBLIC YAML_DECLARE_STATIC
    PRIVATE YAML_VERSION_MAJOR=0 YAML_VERSION_MINOR=2 YAML_VERSION_PATCH=5 YAML_VERSION_STRING="0.2.5")
if(MSVC)
    target_compile_options(yaml PRIVATE /w)
else()
    target_compile_options(yaml PRIVATE -w)
endif()
