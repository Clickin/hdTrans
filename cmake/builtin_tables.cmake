# Writes OUT, a C++ source holding text files as arrays with one string literal per line.
#   cmake -DDIR=<dir> -DOUT=<file> -DTABLES=<array> -DACCESSOR=<function> -P builtin_tables.cmake
#     the *.tsv files of DIR in name order as BuiltinTable arrays, and the function ACCESSOR
#     that returns them
#   cmake -DFILE=<file> -DOUT=<file> -DHEADER=<include> -DNAMESPACE=<namespace> -DACCESSOR=<function>
#         -P builtin_tables.cmake
#     the lines of FILE, each ending in "\n" but a missing last one, and the function ACCESSOR
#     (declared in HEADER) that returns them as std::span<const std::string_view>

# Sets VAR to the initializer of a std::string_view array holding the lines of the file at path.
function(hdtrans_literal_lines path var)
    file(READ "${path}" text)
    if(text STREQUAL "")
        message(FATAL_ERROR "${path} is empty")
    endif()
    string(REPLACE "\\" "\\\\" text "${text}")
    string(REPLACE "\"" "\\\"" text "${text}")
    string(REPLACE "\t" "\\t" text "${text}")
    string(REPLACE "\r" "\\r" text "${text}")
    set(end "\",\n")
    string(LENGTH "${text}" size)
    math(EXPR last "${size} - 1")
    string(SUBSTRING "${text}" ${last} 1 tail)
    if(tail STREQUAL "\n")
        string(SUBSTRING "${text}" 0 ${last} text)
        set(end "\\n\",\n")
    endif()
    string(REPLACE "\n" "\\n\",\n    \"" text "${text}")
    set(${var} "{\n    \"${text}${end}}" PARENT_SCOPE)
endfunction()

if(DEFINED FILE)
    hdtrans_literal_lines("${FILE}" lines)
    file(WRITE "${OUT}" "// Generated table; do not edit.\n#include <span>\n#include <string_view>\n\n"
        "#include \"${HEADER}\"\n\nnamespace ${NAMESPACE} {\n\nnamespace {\n\n"
        "constexpr std::string_view kLines[] = ${lines};\n\n}  // namespace\n\n"
        "std::span<const std::string_view> ${ACCESSOR}() { return kLines; }\n\n}  // namespace ${NAMESPACE}\n")
    return()
endif()

file(GLOB files LIST_DIRECTORIES false "${DIR}/*.tsv")
set(out "// Generated table; do not edit.\n#include \"core/dict/builtin.h\"\n\nnamespace hdTrans::dict::internal {\n\nnamespace {\n\n")
set(entries "")
foreach(path IN LISTS files)
    get_filename_component(name "${path}" NAME)
    get_filename_component(base "${path}" NAME_WE)
    string(SUBSTRING "${base}" 0 1 first)
    string(SUBSTRING "${base}" 1 -1 rest)
    string(TOUPPER "${first}" first)
    set(symbol "k${first}${rest}")
    hdtrans_literal_lines("${path}" lines)
    string(APPEND out "constexpr std::string_view ${symbol}[] = ${lines};\n\n")
    string(APPEND entries "    {\"${name}\", ${symbol}},\n")
endforeach()
string(APPEND out "constexpr BuiltinTable ${TABLES}[] = {\n${entries}};\n\n}  // namespace\n\n"
    "std::span<const BuiltinTable> ${ACCESSOR}() { return ${TABLES}; }\n\n}  // namespace hdTrans::dict::internal\n")
file(WRITE "${OUT}" "${out}")
