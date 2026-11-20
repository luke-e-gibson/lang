cmake_minimum_required(VERSION 3.16)

project(compiler)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(compiler src/main.cpp)
target_link_libraries(compiler PRIVATE llvm_deps)