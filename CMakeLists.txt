cmake_minimum_required(VERSION 3.20)
project(wgslcheck
    VERSION 0.1.0
    DESCRIPTION "Static analyzer and CPU-side debugger for WGSL compute shaders"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Sources. As modules get implemented, add their .cpp here.
set(WGSLCHECK_SOURCES
    src/main.cpp
    # src/lexer.cpp
    # src/parser.cpp
    # src/analyzer.cpp
    # src/simulator.cpp
)

add_executable(wgslcheck ${WGSLCHECK_SOURCES})
target_include_directories(wgslcheck PRIVATE include)

# Keep warnings strict — this is a debugger; correctness matters.
if (MSVC)
    target_compile_options(wgslcheck PRIVATE /W4 /permissive-)
else()
    target_compile_options(wgslcheck PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Expose the version string to C++ code as WGSLCHECK_VERSION.
target_compile_definitions(wgslcheck PRIVATE
    WGSLCHECK_VERSION="${PROJECT_VERSION}")