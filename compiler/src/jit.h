// jit.h
#pragma once
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <string>

// Runs "main" from the module. Returns false and fills err on failure.
bool jitRun(llvm::orc::ThreadSafeModule tsm, int& exitCode, std::string& err);
void jitInit();   // call once at startup