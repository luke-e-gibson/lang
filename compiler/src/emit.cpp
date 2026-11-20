#include "emit.h"
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
using namespace llvm;

bool emitObjectFile(Module& mod, const std::string& path, std::string& err) {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();

    Triple triple(sys::getDefaultTargetTriple());

    std::string lookupErr;
    const Target* target = TargetRegistry::lookupTarget(triple, lookupErr);
    if (!target) { err = lookupErr; return false; }

    TargetOptions opts;
    std::unique_ptr<TargetMachine> tm(
        target->createTargetMachine(triple, "generic", "", opts, Reloc::PIC_));
    if (!tm) { err = "could not create target machine"; return false; }

    mod.setTargetTriple(triple);
    mod.setDataLayout(tm->createDataLayout());

    std::error_code ec;
    raw_fd_ostream out(path, ec, sys::fs::OF_None);
    if (ec) { err = ec.message(); return false; }

    legacy::PassManager pm;
    if (tm->addPassesToEmitFile(pm, out, nullptr, CodeGenFileType::ObjectFile)) {
        err = "target cannot emit object files";
        return false;
    }
    pm.run(mod);
    out.flush();
    return true;
}