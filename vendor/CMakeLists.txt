set(LLVM_TARGETS_TO_BUILD "X86" CACHE STRING "" FORCE)
set(LLVM_INCLUDE_TESTS OFF CACHE BOOL "" FORCE)
set(LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLVM_INCLUDE_BENCHMARKS OFF CACHE BOOL "" FORCE)

add_subdirectory(llvm/llvm EXCLUDE_FROM_ALL)

add_library(llvm_deps INTERFACE)
target_include_directories(llvm_deps SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/llvm/llvm/include
    ${CMAKE_CURRENT_BINARY_DIR}/llvm/llvm/include)
target_link_libraries(llvm_deps INTERFACE
    LLVMCore LLVMSupport LLVMIRReader LLVMPasses
    LLVMOrcJIT LLVMExecutionEngine
    LLVMX86CodeGen LLVMX86AsmParser LLVMX86Desc LLVMX86Info)