cmake_minimum_required(VERSION 3.16)

project(compiler)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Glob Search for all source files in the src directory
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.h")

add_executable(compiler ${SOURCES})
target_link_libraries(compiler PRIVATE llvm_deps)