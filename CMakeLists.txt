# Static/shared engine libraries, CLI and unit tests on any C++20 compiler;
# J2KEngine.dll (J2K_* exports) on Windows; WebAssembly with Emscripten. Presets: CMakePresets.json.
cmake_minimum_required(VERSION 3.25)
project(hdtrans LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(HDTRANS_WERROR "Treat compiler warnings as errors" ON)
option(HDTRANS_INTERPRET_RULES "Evaluate every selection rule with the interpreter, ignoring compiled rules" OFF)

if(MSVC)
    # Static CRT: the DLL is one file inside a host that ships no runtime of ours.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # Replace CMake's defaults so the command line holds only what we ask for.
    set(CMAKE_CXX_FLAGS "/EHs-c- /GR- /utf-8 /permissive- /W4 /Zc:inline /diagnostics:column")
    set(CMAKE_C_FLAGS "/utf-8 /W4 /diagnostics:column")
    if(HDTRANS_WERROR)
        string(APPEND CMAKE_CXX_FLAGS " /WX")
        string(APPEND CMAKE_C_FLAGS " /WX")
    endif()
    if(CMAKE_GENERATOR MATCHES "^Visual Studio ")
        string(APPEND CMAKE_CXX_FLAGS " /MP")
        string(APPEND CMAKE_C_FLAGS " /MP")
    endif()
    set(CMAKE_C_FLAGS_RELEASE "/O2 /Oi /Gy /DNDEBUG")
    set(CMAKE_C_FLAGS_DEBUG "/Od /RTC1 /Zi /D_DEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Oi /Gy /DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG "/Od /RTC1 /Zi /D_DEBUG")
    add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS _HAS_EXCEPTIONS=0)
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "/INCREMENTAL:NO /OPT:REF /OPT:ICF /DEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/INCREMENTAL:NO /OPT:REF /OPT:ICF /DEBUG")
else()
    add_compile_options(-Wall -Wextra -fno-exceptions -fno-rtti)
    if(HDTRANS_WERROR)
        add_compile_options(-Werror)
    endif()
endif()

# The builtin dictionary tables, src/core/dict/builtin/*.tsv and builtin/extra/*.tsv, compiled in.
set(HDTRANS_BUILTIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/core/dict/builtin")
file(GLOB HDTRANS_BUILTIN_TSV CONFIGURE_DEPENDS "${HDTRANS_BUILTIN_DIR}/*.tsv")
file(GLOB HDTRANS_BUILTIN_EXTRA_TSV CONFIGURE_DEPENDS "${HDTRANS_BUILTIN_DIR}/extra/*.tsv")
# A stamp lists the table files; it changes only when a table is added or removed, so that a
# removed table also regenerates the source.
function(hdtrans_builtin_source output dir array accessor inputs)
    set(script "${CMAKE_CURRENT_SOURCE_DIR}/cmake/builtin_tables.cmake")
    set(stamp "${CMAKE_CURRENT_BINARY_DIR}/${output}.tables")
    string(REPLACE ";" "\n" names "${${inputs}}")
    file(CONFIGURE OUTPUT "${stamp}" CONTENT "${names}\n" @ONLY)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${output}"
        COMMAND "${CMAKE_COMMAND}" "-DDIR=${dir}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/${output}"
            "-DTABLES=${array}" "-DACCESSOR=${accessor}" -P "${script}"
        DEPENDS ${${inputs}} "${stamp}" "${script}" VERBATIM)
endfunction()
hdtrans_builtin_source(builtin_gen.cpp "${HDTRANS_BUILTIN_DIR}" kTables BuiltinTables HDTRANS_BUILTIN_TSV)
hdtrans_builtin_source(builtin_extra_gen.cpp "${HDTRANS_BUILTIN_DIR}/extra" kFiles ExtraBuiltinFiles
    HDTRANS_BUILTIN_EXTRA_TSV)
# The rules program of the word stage, the *.txt files of src/core/rules, compiled in; a stamp
# lists the files so that adding or removing one regenerates the source.
set(HDTRANS_RULE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/core/rules")
file(GLOB HDTRANS_RULES CONFIGURE_DEPENDS "${HDTRANS_RULE_DIR}/*.txt")
list(SORT HDTRANS_RULES)
string(REPLACE ";" "\n" HDTRANS_RULE_NAMES "${HDTRANS_RULES}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rules_gen.cpp.files" CONTENT "${HDTRANS_RULE_NAMES}\n" @ONLY)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rules_gen.cpp"
    COMMAND "${CMAKE_COMMAND}" "-DRULE_DIR=${HDTRANS_RULE_DIR}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/rules_gen.cpp"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/builtin_tables.cmake"
    DEPENDS ${HDTRANS_RULES} "${CMAKE_CURRENT_BINARY_DIR}/rules_gen.cpp.files"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/builtin_tables.cmake" VERBATIM)

add_subdirectory(third_party/pcre2)

file(GLOB_RECURSE HDTRANS_SOURCES CONFIGURE_DEPENDS src/core/*.cpp)
add_library(hdtrans STATIC ${HDTRANS_SOURCES}
    "${CMAKE_CURRENT_BINARY_DIR}/builtin_gen.cpp" "${CMAKE_CURRENT_BINARY_DIR}/builtin_extra_gen.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/rules_gen.cpp")
target_link_libraries(hdtrans PRIVATE pcre2-16)
if(HDTRANS_INTERPRET_RULES)
    target_compile_definitions(hdtrans PRIVATE HDTRANS_INTERPRET_RULES)
endif()
file(GLOB_RECURSE HDTRANS_DATA_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/core/*.h)
list(APPEND HDTRANS_DATA_SOURCES ${HDTRANS_RULES})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${HDTRANS_DATA_SOURCES})
set(HDTRANS_DATA_HASHES "")
foreach(dataFile IN LISTS HDTRANS_DATA_SOURCES)
    file(SHA256 "${dataFile}" dataHash)
    file(RELATIVE_PATH dataName "${CMAKE_CURRENT_SOURCE_DIR}" "${dataFile}")
    string(APPEND HDTRANS_DATA_HASHES "${dataName}:${dataHash}\n")
endforeach()
string(SHA256 HDTRANS_DATA_ID "${HDTRANS_DATA_HASHES}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${HDTRANS_BUILTIN_TSV} ${HDTRANS_BUILTIN_EXTRA_TSV})
set(HDTRANS_BUILTIN_HASHES "")
foreach(tableFile IN LISTS HDTRANS_BUILTIN_TSV HDTRANS_BUILTIN_EXTRA_TSV)
    file(SHA256 "${tableFile}" tableHash)
    file(RELATIVE_PATH tableName "${HDTRANS_BUILTIN_DIR}" "${tableFile}")
    string(APPEND HDTRANS_BUILTIN_HASHES "${tableName}:${tableHash}\n")
endforeach()
string(SHA256 HDTRANS_BUILTIN_ID "${HDTRANS_BUILTIN_HASHES}")
string(SHA256 HDTRANS_COMPILER_ID "${CMAKE_CXX_COMPILER_ID}:${CMAKE_CXX_COMPILER_VERSION}:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}:${CMAKE_SIZEOF_VOID_P}:${CMAKE_BUILD_TYPE}:${CMAKE_CXX_FLAGS}:${CMAKE_CXX_FLAGS_RELEASE}:${CMAKE_CXX_FLAGS_DEBUG}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/dictionary_ids.h" CONTENT
"#pragma once\n#define HDTRANS_DATA_ID \"${HDTRANS_DATA_ID}\"\n#define HDTRANS_BUILTIN_ID \"${HDTRANS_BUILTIN_ID}\"\n#define HDTRANS_COMPILER_ID \"${HDTRANS_COMPILER_ID}\"\n")
set_target_properties(hdtrans PROPERTIES OUTPUT_NAME hdTransCore COMPILE_PDB_NAME hdTransCore)
target_include_directories(hdtrans PUBLIC include PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}")

if(NOT EMSCRIPTEN)
    set_target_properties(hdtrans PROPERTIES POSITION_INDEPENDENT_CODE ON)
    find_package(Threads REQUIRED)
    target_link_libraries(hdtrans PUBLIC Threads::Threads)

    add_executable(hdtrans_cli src/cli/main.cpp)
    target_include_directories(hdtrans_cli PRIVATE src)
    target_link_libraries(hdtrans_cli PRIVATE hdtrans)
    set_target_properties(hdtrans_cli PROPERTIES OUTPUT_NAME hdtrans PDB_NAME hdtrans_cli)

    file(GLOB_RECURSE HDTRANS_TEST_SOURCES CONFIGURE_DEPENDS tests/unit/*.cpp)
    add_executable(hdtrans_tests ${HDTRANS_TEST_SOURCES})
    target_include_directories(hdtrans_tests PRIVATE tests/unit src)
    target_link_libraries(hdtrans_tests PRIVATE hdtrans)
    enable_testing()
    add_test(NAME unit COMMAND hdtrans_tests)
    if(UNIX)
        add_test(NAME tools.selfbase COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/selfbase.sh" "$<TARGET_FILE:hdtrans_cli>")
    endif()

    # Translates examples/ with the dictionaries in HDTRANS_DICTS at test time; skipped when unset.
    find_program(HDTRANS_SH sh)
    if(HDTRANS_SH)
        add_test(NAME examples COMMAND "${HDTRANS_SH}" -c "exec \"$0\" \"$1\" \"$2\" \"$HDTRANS_DICTS\""
            "${HDTRANS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/examples.sh" $<TARGET_FILE:hdtrans_cli>)
        set_tests_properties(examples PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300
            ENVIRONMENT "HDTRANS_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/examples-cache")
        # Pins the word choices of the rules program: tests/rules/cases.tsv with dicts/.
        add_test(NAME rules.cases COMMAND "${HDTRANS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/rule-cases.sh"
            $<TARGET_FILE:hdtrans_cli> "${CMAKE_CURRENT_SOURCE_DIR}/dicts")
        # One table per theme file, tests/rules/cases/<theme>.tsv, written as rules are fixed.
        add_test(NAME rules.cases.themes COMMAND "${HDTRANS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/rule-cases.sh"
            $<TARGET_FILE:hdtrans_cli> "${CMAKE_CURRENT_SOURCE_DIR}/dicts" "${CMAKE_CURRENT_SOURCE_DIR}/tests/rules/cases")
        set_tests_properties(rules.cases rules.cases.themes PROPERTIES TIMEOUT 300
            ENVIRONMENT "HDTRANS_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/examples-cache")
        # Compares with the expected outputs in HDTRANS_TESTDATA at test time; skipped when unset.
        add_test(NAME testdata COMMAND "${HDTRANS_SH}" -c "exec \"$0\" \"$1\" --expected \"$HDTRANS_TESTDATA\" \"$2\" \"$3\""
            "${HDTRANS_SH}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/e2e.sh" $<TARGET_FILE:hdtrans_cli>
            "${CMAKE_CURRENT_SOURCE_DIR}/dicts")
        set_tests_properties(testdata PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
    endif()

    add_executable(admin_priority tests/host/admin_priority.cpp src/cshared/hdtrans_c.cpp)
    target_include_directories(admin_priority PRIVATE src)
    target_link_libraries(admin_priority PRIVATE hdtrans ${CMAKE_DL_LIBS})
    add_test(NAME host.admin_priority COMMAND admin_priority "${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/dict/testdata/folder")
    set_tests_properties(host.admin_priority PROPERTIES TIMEOUT 60 PROCESSORS 8
        ENVIRONMENT "HDTRANS_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/admin-priority-cache")

    # The rule compiler (docs/rule-compiler.md), a host tool. The checks run on the shipped rules,
    # and the committed compiled rules must be its output for them.
    if(NOT CMAKE_CROSSCOMPILING)
        # It builds from the rule and code sources only, not from the library that holds its output.
        file(GLOB HDTRANS_RULEC_SOURCES CONFIGURE_DEPENDS src/rulec/*.cpp)
        add_executable(hdrulec ${HDTRANS_RULEC_SOURCES} src/core/rules/load.cpp src/core/rules/eval.cpp
            src/core/rules/match.cpp src/core/rules/attr_families_gen.cpp src/core/dict/codes.cpp
            src/core/dict/pos_tags_gen.cpp)
        target_include_directories(hdrulec PRIVATE src)
        add_test(NAME rules.lint COMMAND hdrulec lint "${HDTRANS_RULE_DIR}" -dicts "${CMAKE_CURRENT_SOURCE_DIR}/dicts")
        add_test(NAME rules.verify COMMAND hdrulec verify "${HDTRANS_RULE_DIR}" -paths 4096)
        add_test(NAME rules.identity COMMAND hdrulec dump "${HDTRANS_RULE_DIR}" -check "${HDTRANS_RULE_DIR}/program.hash")
        add_test(NAME rules.generated COMMAND hdrulec cpp "${HDTRANS_RULE_DIR}" -local 0-499 -globals
            -check "${CMAKE_CURRENT_SOURCE_DIR}/src/core/rules/compiled_gen.cpp")
        # The test lines of the rules, one unit test each (ruletest_L3_1, ...), run with dicts/.
        add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rule_tests_gen.cpp"
            COMMAND hdrulec tests "${HDTRANS_RULE_DIR}" -o "${CMAKE_CURRENT_BINARY_DIR}/rule_tests_gen.cpp"
            DEPENDS hdrulec ${HDTRANS_RULES} VERBATIM)
        target_sources(hdtrans_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/rule_tests_gen.cpp")
        add_test(NAME rules.tests COMMAND hdtrans_tests ruletest_)
        set_tests_properties(rules.tests PROPERTIES ENVIRONMENT
            "HDTRANS_DICTS=${CMAKE_CURRENT_SOURCE_DIR}/dicts;HDTRANS_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/rules-cache")
    endif()

    # Dumps compiled dictionary entries and lookups over input lines.
    add_executable(snapshot_check tests/snapshot_check/main.cpp)
    target_include_directories(snapshot_check PRIVATE src)
    target_link_libraries(snapshot_check PRIVATE hdtrans)
endif()

if(EMSCRIPTEN)
    add_executable(hdtrans_wasm src/wasm/main.cpp src/cshared/hdtrans_c.cpp)
    target_include_directories(hdtrans_wasm PRIVATE src)
    set_target_properties(hdtrans_wasm PROPERTIES OUTPUT_NAME hdTransEngine)
    target_link_libraries(hdtrans_wasm PRIVATE hdtrans)
    target_link_options(hdtrans_wasm PRIVATE
        --no-entry -sMODULARIZE=1 -sEXPORT_ES6=1 -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1
        -sEXPORTED_RUNTIME_METHODS=FS,ccall,cwrap,UTF8ToString,stringToUTF8,lengthBytesUTF8,getValue,HEAPU8
        -sEXPORTED_FUNCTIONS=_hdTrans_Load,_hdTrans_Initialize,_hdTrans_Terminate,_hdTrans_Translate,_hdTrans_Reset,_hdTrans_Reload,_hdTrans_UserDictError,_hdTrans_OpenEhnd,_hdTrans_FilterErrors,_hdTrans_FreeMem,_hdTrans_Version,_malloc,_free)
elseif(WIN32)
    add_library(hdtrans_dll SHARED src/cshared/hdtrans_c.cpp src/cshared/hdtrans.def)
    set_target_properties(hdtrans_dll PROPERTIES OUTPUT_NAME hdTransEngine PREFIX "")
    target_compile_definitions(hdtrans_dll PRIVATE HDTRANS_BUILD_DLL)
    target_include_directories(hdtrans_dll PRIVATE src)
    target_link_libraries(hdtrans_dll PRIVATE hdtrans)

    add_library(j2kengine SHARED src/j2kengine/j2kengine.cpp src/j2kengine/j2kengine.def)
    set_target_properties(j2kengine PROPERTIES OUTPUT_NAME J2KEngine PREFIX "")
    target_include_directories(j2kengine PRIVATE src)
    target_link_libraries(j2kengine PRIVATE hdtrans)

    add_executable(j2k_host tests/host/j2k_host.c)
    target_compile_definitions(j2k_host PRIVATE UNICODE _UNICODE)
    add_dependencies(j2k_host j2kengine hdtrans_dll)
    add_test(NAME host.j2k COMMAND j2k_host $<TARGET_FILE:j2kengine>)
    set_tests_properties(host.j2k PROPERTIES ENVIRONMENT "HDTRANS_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/host-cache")
else()
    add_library(hdtrans_shared SHARED src/cshared/hdtrans_c.cpp)
    set_target_properties(hdtrans_shared PROPERTIES OUTPUT_NAME hdTransEngine)
    target_include_directories(hdtrans_shared PRIVATE src)
    target_link_libraries(hdtrans_shared PRIVATE hdtrans ${CMAKE_DL_LIBS})
endif()
