#pragma once
#include "ast.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <map>
#include <string>
#include <vector>

class CodeGen {
public:
    CodeGen();
    bool generate(const Program& prog);          // false on error
    void print() { module_->print(llvm::outs(), nullptr); }
    const std::vector<std::string>& errors() const { return errors_; }

private:
    // Order matters: context must be declared first, so it is destroyed last.
    llvm::LLVMContext ctx_;
    std::unique_ptr<llvm::Module> module_;
    llvm::IRBuilder<> builder_;

    struct Var { llvm::AllocaInst* slot; llvm::Type* type; };
    std::vector<std::map<std::string, Var>> scopes_;
    std::vector<std::string> errors_;

    llvm::Value* fail(const std::string& msg, uint32_t offset);
    Var* lookup(const std::string& name);
    llvm::AllocaInst* createEntryAlloca(llvm::Type* ty, const std::string& name);

    llvm::Value* genExpr(const Expr& e);
    llvm::Value* genBinary(const BinaryExpr& e);
    llvm::Value* genCall(const CallExpr& e);
    llvm::Value* genPrint(const CallExpr& e);
    bool genStmt(const Stmt& s);
    bool genIf(const IfStmt& s);
    bool terminated();
};