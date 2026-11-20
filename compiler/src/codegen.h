#pragma once
#include "ast.h"
#include "llvm/IR/Value.h"
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <map>
#include <string>
#include <vector>

class CodeGen {
public:
    CodeGen();
    bool generate(const Program& prog);          // false on error
    void print() { module_->print(llvm::outs(), nullptr); }
    llvm::Module& module() { return *module_; }
    const std::vector<std::string>& errors() const { return errors_; }

    llvm::orc::ThreadSafeModule takeModule() {
       return llvm::orc::ThreadSafeModule(std::move(module_), std::move(ctx_));
    }

private:
    // Order matters: context must be declared first, so it is destroyed last.
    std::unique_ptr<llvm::LLVMContext> ctx_;
    std::unique_ptr<llvm::Module> module_;
    llvm::IRBuilder<> builder_;

    struct Var { llvm::AllocaInst* slot; llvm::Type* type; };
    std::vector<std::map<std::string, Var>> scopes_;
    std::vector<std::string> errors_;
    llvm::Function* currentFn_ = nullptr;

    llvm::Value* fail(const std::string& msg, uint32_t offset);
    Var* lookup(const std::string& name);
    llvm::AllocaInst* createEntryAlloca(llvm::Type* ty, const std::string& name);

    llvm::Value* genExpr(const Expr& e);
    llvm::Value* genBinary(const BinaryExpr& e);
    llvm::Value* genCall(const CallExpr& e);
    llvm::Value* genPrint(const CallExpr& e);
    void genFunction(const FunctionDecl& f);

    llvm::Type* lowerType(const std::string& name);
    llvm::Value* coerce(llvm::Value* v, llvm::Type* to);

    bool genStmt(const Stmt& s);
    bool genIf(const IfStmt& s);
    bool terminated();
};