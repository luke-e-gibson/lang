// jit.cpp
#include "jit.h"
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/Error.h>
#include <cstdarg>
#include <cstdio>
using namespace llvm;
using namespace llvm::orc;

// printf wrapper. On Windows the exe does not export "printf" for the JIT to find,
// so we register this function under that name ourselves.
extern "C" int jit_printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
    return n;
}

void jitInit() {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
}

bool jitRun(ThreadSafeModule tsm, int& exitCode, std::string& err) {
    auto fail = [&](Error e) {
        err = toString(std::move(e));
        return false;
    };

    auto jitOrErr = LLJITBuilder().create();
    if (!jitOrErr) return fail(jitOrErr.takeError());
    auto jit = std::move(*jitOrErr);

    // Expose host functions to JIT'd code.
    SymbolMap syms;
    syms[jit->mangleAndIntern("printf")] = {
        ExecutorAddr::fromPtr(&jit_printf), JITSymbolFlags::Exported};
    if (auto e = jit->getMainJITDylib().define(absoluteSymbols(std::move(syms))))
        return fail(std::move(e));

    if (auto e = jit->addIRModule(std::move(tsm))) return fail(std::move(e));

    auto sym = jit->lookup("main");
    if (!sym) return fail(sym.takeError());

    auto* mainFn = sym->toPtr<int (*)()>();
    exitCode = mainFn();
    return true;
}