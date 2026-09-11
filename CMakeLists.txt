cmake_minimum_required(VERSION 3.20)

project(tempo VERSION 0.1.0 LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(CTest)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20 CACHE STRING "C++ standard used to build tempo")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TEMPO_BUILD_TESTS "Build tempo's test suite" ${BUILD_TESTING})
option(TEMPO_BUILD_EXAMPLES "Build tempo's examples" ON)
option(TEMPO_BUILD_DIAGNOSTICS "Run compile-failure diagnostic tests" ON)
set(TEMPO_ENABLED "1" CACHE STRING "Enable tempo's instrumentation macros")
set(TEMPO_PRINT_ENABLED "0" CACHE STRING "Print every tempo measurement")
set(TEMPO_COUNT_RECURSION "0" CACHE STRING "Count recursive calls")
set(TEMPO_WORST_CALLS "10" CACHE STRING "Default number of retained slow calls")

find_package(Threads REQUIRED)

add_library(tempo INTERFACE)
add_library(tempo::tempo ALIAS tempo)
target_compile_features(tempo INTERFACE cxx_std_20)
target_include_directories(tempo INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(tempo INTERFACE Threads::Threads)
target_compile_definitions(tempo INTERFACE
    TEMPO_ENABLED=${TEMPO_ENABLED}
    TEMPO_PRINT_ENABLED=${TEMPO_PRINT_ENABLED}
    TEMPO_COUNT_RECURSION=${TEMPO_COUNT_RECURSION}
    TEMPO_WORST_CALLS=${TEMPO_WORST_CALLS})

function(tempo_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
endfunction()

if(TEMPO_BUILD_TESTS)
    file(GLOB tempo_test_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/[0-9]*.cpp")

    foreach(source IN LISTS tempo_test_sources)
        get_filename_component(name "${source}" NAME_WE)
        set(target "tempo-test-${name}")
        add_executable(${target} "${source}")
        target_link_libraries(${target} PRIVATE tempo::tempo)
        tempo_enable_warnings(${target})
        add_test(NAME "test.${name}" COMMAND ${target})
    endforeach()

    if(TEMPO_BUILD_DIAGNOSTICS)
        # This script invokes the active C++ compiler directly because the
        # inputs are intentionally invalid. MSVC runners usually need a
        # developer-shell environment for direct cl.exe use, so CI disables this
        # subtest there while still building and running the normal suite.
        add_test(NAME diagnostics
            COMMAND ${CMAKE_COMMAND}
                "-DTEMPO_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
                "-DTEMPO_CXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}"
                "-DTEMPO_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-diagnostics.cmake")
        set_tests_properties(diagnostics PROPERTIES LABELS "diagnostics")
    endif()
endif()

if(TEMPO_BUILD_EXAMPLES)
    file(GLOB tempo_example_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/[0-9]*.cpp")

    foreach(source IN LISTS tempo_example_sources)
        get_filename_component(name "${source}" NAME_WE)
        set(target "tempo-example-${name}")
        add_executable(${target} "${source}")
        target_link_libraries(${target} PRIVATE tempo::tempo)
        tempo_enable_warnings(${target})
        if(TEMPO_BUILD_TESTS)
            add_test(NAME "example.${name}" COMMAND ${target})
        endif()
    endforeach()
endif()

install(TARGETS tempo EXPORT tempoTargets)
install(FILES tempo.hpp DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(EXPORT tempoTargets
    FILE tempoTargets.cmake
    NAMESPACE tempo::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tempo")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tempoConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/tempoConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tempo")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/tempoConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/tempoConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/tempoConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tempo")
