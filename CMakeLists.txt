# ==============================================================================
# Minimum required CMake version.
# We need 3.20+ for better support of modern C++ features and modern policies.
# ==============================================================================
cmake_minimum_required(VERSION 3.20)

# ==============================================================================
# Project definition.
# Sets the project name, version, description, and the language (C++).
# ==============================================================================
project(wgslcheck
    VERSION 0.1.0
    DESCRIPTION "Static analyzer and CPU-side debugger for WGSL compute shaders"
    LANGUAGES CXX)

# ==============================================================================
# C++ Standard Configuration.
# Enforce C++20 without compiler-specific extensions (CMAKE_CXX_EXTENSIONS OFF)
# to guarantee maximum portability across GCC, Clang, and MSVC.
# ==============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==============================================================================
# Source Files.
# Collect all implementation files for the main executable.
# Grouping them in a variable makes it easier to reuse if we add more targets.
# ==============================================================================
set(WGSLCHECK_SOURCES
    src/main.cpp
    src/lexer.cpp
    src/parser.cpp
    src/analyzer.cpp
    src/simulator.cpp
)

# ==============================================================================
# Main Executable Target.
# Creates the `wgslcheck` binary from the sources listed above.
# ==============================================================================
add_executable(wgslcheck ${WGSLCHECK_SOURCES})

# ==============================================================================
# Include Directories.
# PRIVATE means this include path is only used to compile `wgslcheck`, 
# and won't be exposed if someone links against this target (e.g., as a library).
# ==============================================================================
target_include_directories(wgslcheck PRIVATE include)

# ==============================================================================
# Compiler Warnings.
# Enforce strict warnings because correctness is paramount for a static analyzer.
# MSVC uses /W4 and /permissive- for standard compliance.
# GCC/Clang use -Wall, -Wextra, and -Wpedantic.
# ==============================================================================
if (MSVC)
    target_compile_options(wgslcheck PRIVATE /W4 /permissive-)
else()
    target_compile_options(wgslcheck PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ==============================================================================
# Version Injection.
# Pass the project version defined at the top of this file to the C++ compiler
# so it can be printed by `wgslcheck --version`.
# ==============================================================================
target_compile_definitions(wgslcheck PRIVATE
    WGSLCHECK_VERSION="${PROJECT_VERSION}")

# ==============================================================================
# Testing Support.
# Uses CTest (CMake's built-in testing framework).
# Only configured if BUILD_TESTING is ON (which is the default).
# ==============================================================================
include(CTest)
if (BUILD_TESTING)
    # --------------------------------------------------------------------------
    # Unit Test Executable.
    # Compiles the test runner alongside the implementation files.
    # Note: In a larger project, these sources would be extracted into a 
    # static library (e.g., `add_library(wgslcheck_core STATIC ...)`) and linked 
    # to both the main executable and the tests.
    # --------------------------------------------------------------------------
    add_executable(wgslcheck_tests tests/test_wgslcheck.cpp
        src/lexer.cpp
        src/parser.cpp
        src/analyzer.cpp
        src/simulator.cpp)
        
    target_include_directories(wgslcheck_tests PRIVATE include)
    
    if (MSVC)
        target_compile_options(wgslcheck_tests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(wgslcheck_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    
    # --------------------------------------------------------------------------
    # Test Registration.
    # Registers the unit tests and CLI integration tests with CTest.
    # The CLI tests run the built `wgslcheck` binary against the sample file.
    # --------------------------------------------------------------------------
    add_test(NAME wgslcheck_tests COMMAND wgslcheck_tests)
    add_test(NAME wgslcheck_cli_check
        COMMAND wgslcheck check "${CMAKE_CURRENT_SOURCE_DIR}/examples/sample.wgsl")
    add_test(NAME wgslcheck_cli_sim
        COMMAND wgslcheck sim "${CMAKE_CURRENT_SOURCE_DIR}/examples/sample.wgsl"
                --dispatch 2 1 1)
    add_test(NAME wgslcheck_cli_missing_file
        COMMAND "${CMAKE_COMMAND}"
            "-DEXECUTABLE=$<TARGET_FILE:wgslcheck>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli_missing_file.cmake")
    add_test(NAME wgslcheck_cli_regressions
        COMMAND "${CMAKE_COMMAND}"
            "-DEXECUTABLE=$<TARGET_FILE:wgslcheck>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli_regressions.cmake")
endif()
