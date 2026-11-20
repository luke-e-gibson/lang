// emit.h
#pragma once
#include <llvm/IR/Module.h>
#include <string>

bool emitObjectFile(llvm::Module& mod, const std::string& path, std::string& err);