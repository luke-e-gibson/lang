cmake_minimum_required(VERSION 3.16)

project(compiler)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(LANG_LINK_OUTPUT_OPTION "/Fe:")
    set(LANG_LINK_OUTPUT_NEEDS_SPACE 0)
    set(LANG_LINK_FLAGS "/link /SUBSYSTEM:CONSOLE /DEFAULTLIB:libcmt /DEFAULTLIB:legacy_stdio_definitions")
else()
    set(LANG_LINK_OUTPUT_OPTION "-o")
    set(LANG_LINK_OUTPUT_NEEDS_SPACE 1)
    set(LANG_LINK_FLAGS "")
endif()

configure_file(compiler_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/compiler_config.h @ONLY)

# Glob Search for all source files in the src directory
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.h")

add_executable(compiler ${SOURCES})
target_include_directories(compiler PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(compiler PRIVATE llvm_deps)
