if(NOT DEFINED TEMPO_CXX_COMPILER OR NOT EXISTS "${TEMPO_CXX_COMPILER}")
    message(FATAL_ERROR "TEMPO_CXX_COMPILER must name the active C++ compiler")
endif()
if(NOT DEFINED TEMPO_SOURCE_DIR OR NOT EXISTS "${TEMPO_SOURCE_DIR}/tempo.hpp")
    message(FATAL_ERROR "TEMPO_SOURCE_DIR must name the tempo source directory")
endif()

file(GLOB diagnostic_sources LIST_DIRECTORIES false
    "${TEMPO_SOURCE_DIR}/tests/errors/[0-9]*.cpp")
list(SORT diagnostic_sources)

set(failures "")
foreach(source IN LISTS diagnostic_sources)
    file(READ "${source}" contents)
    string(REGEX MATCH "// EXPECT: ([^\r\n]+)" expected_match "${contents}")
    set(expected "${CMAKE_MATCH_1}")
    get_filename_component(name "${source}" NAME)

    if(expected STREQUAL "")
        list(APPEND failures "${name}: no // EXPECT: line")
        continue()
    endif()

    if(TEMPO_CXX_COMPILER_ID STREQUAL "MSVC")
        set(compiler_arguments /nologo /std:c++20 /Zs "/I${TEMPO_SOURCE_DIR}" "${source}")
        set(error_pattern "[Ee]rror C[0-9]+:")
    else()
        set(compiler_arguments -std=c++20 -fsyntax-only "-I${TEMPO_SOURCE_DIR}" "${source}")
        set(error_pattern "error:")
    endif()

    execute_process(
        COMMAND "${TEMPO_CXX_COMPILER}" ${compiler_arguments}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    set(output "${stdout}${stderr}")

    if(result EQUAL 0)
        list(APPEND failures "${name}: compiled, but was expected to fail")
        continue()
    endif()

    string(FIND "${output}" "${expected}" expected_position)
    if(expected_position EQUAL -1)
        list(APPEND failures "${name}: missing expected diagnostic: ${expected}")
        continue()
    endif()

    string(REGEX MATCHALL "${error_pattern}" errors "${output}")
    list(LENGTH errors error_count)
    if(NOT error_count EQUAL 1)
        list(APPEND failures "${name}: ${error_count} errors, expected exactly 1")
        continue()
    endif()

    message(STATUS "diagnostic passed: ${name}")
endforeach()

if(failures)
    string(JOIN "\n  " failure_text ${failures})
    message(FATAL_ERROR "Diagnostic checks failed:\n  ${failure_text}")
endif()

message(STATUS "All tempo diagnostic checks passed")
