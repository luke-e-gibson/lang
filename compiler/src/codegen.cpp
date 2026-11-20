#include "codegen.h"
#include <llvm/IR/Verifier.h>
using namespace llvm;

CodeGen::CodeGen()
    : ctx_(std::make_unique<LLVMContext>()),
      module_(std::make_unique<Module>("lang", *ctx_)),
      builder_(*ctx_) {}

Value* CodeGen::fail(const std::string& msg, uint32_t offset) {
    errors_.push_back("codegen error at " + std::to_string(offset) + ": " + msg);
    return nullptr;
}

CodeGen::Var* CodeGen::lookup(const std::string& name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto f = it->find(name);
        if (f != it->end()) return &f->second;
    }
    return nullptr;
}

// Allocas belong in the entry block so mem2reg can promote them.
AllocaInst* CodeGen::createEntryAlloca(llvm::Type* ty, const std::string& name) {
    Function* fn = builder_.GetInsertBlock()->getParent();
    IRBuilder<> tmp(&fn->getEntryBlock(), fn->getEntryBlock().begin());
    return tmp.CreateAlloca(ty, nullptr, name);
}

bool CodeGen::generate(const Program& prog) {
    auto* fty = FunctionType::get(builder_.getInt32Ty(), false);
    auto* fn = Function::Create(fty, Function::ExternalLinkage, "main", *module_);
    builder_.SetInsertPoint(BasicBlock::Create(*ctx_, "entry", fn));

    scopes_.emplace_back();
    for (auto& s : prog.items) {
        if (!genStmt(*s)) break;
    }
    scopes_.pop_back();

    if (!errors_.empty()) return false;
    if (!terminated()) builder_.CreateRet(builder_.getInt32(0));

    if (verifyFunction(*fn, &errs()) || verifyModule(*module_, &errs())) {
        errors_.push_back("internal error: generated invalid IR");
        return false;
    }
    return true;
}

// ---------- Expressions ----------

Value* CodeGen::genExpr(const Expr& e) {
    switch (e.kind) {
    case ExprKind::IntLit:
        return builder_.getInt64(static_cast<const IntLit&>(e).value);
    case ExprKind::FloatLit:
        return ConstantFP::get(builder_.getDoubleTy(), static_cast<const FloatLit&>(e).value);
    case ExprKind::BoolLit:
        return builder_.getInt1(static_cast<const BoolLit&>(e).value);
    case ExprKind::StringLit:
        return builder_.CreateGlobalString(static_cast<const StringLit&>(e).value);
    case ExprKind::Ident: {
        auto& id = static_cast<const IdentExpr&>(e);
        Var* v = lookup(id.name);
        if (!v) return fail("undefined variable '" + id.name + "'", e.offset);
        return builder_.CreateLoad(v->type, v->slot, id.name);
    }
    case ExprKind::Unary: {
        auto& u = static_cast<const UnaryExpr&>(e);
        Value* x = genExpr(*u.operand);
        if (!x) return nullptr;
        if (u.op == UnaryOp::Neg)
            return x->getType()->isDoubleTy() ? builder_.CreateFNeg(x) : builder_.CreateNeg(x);
        return builder_.CreateNot(x);
    }
    case ExprKind::Binary: return genBinary(static_cast<const BinaryExpr&>(e));
    case ExprKind::Call:   return genCall(static_cast<const CallExpr&>(e));
    }
    return nullptr;
}

Value* CodeGen::genBinary(const BinaryExpr& e) {
    Value* l = genExpr(*e.lhs);
    Value* r = l ? genExpr(*e.rhs) : nullptr;
    if (!l || !r) return nullptr;

    if (l->getType() != r->getType())
        return fail("operand types do not match", e.offset);   // add promotion later

    if (l->getType()->isDoubleTy()) {
        switch (e.op) {
        case BinaryOp::Add: return builder_.CreateFAdd(l, r);
        case BinaryOp::Sub: return builder_.CreateFSub(l, r);
        case BinaryOp::Mul: return builder_.CreateFMul(l, r);
        case BinaryOp::Div: return builder_.CreateFDiv(l, r);
        case BinaryOp::Eq:  return builder_.CreateFCmpOEQ(l, r);
        case BinaryOp::Ne:  return builder_.CreateFCmpUNE(l, r);
        case BinaryOp::Lt:  return builder_.CreateFCmpOLT(l, r);
        case BinaryOp::Le:  return builder_.CreateFCmpOLE(l, r);
        case BinaryOp::Gt:  return builder_.CreateFCmpOGT(l, r);
        case BinaryOp::Ge:  return builder_.CreateFCmpOGE(l, r);
        }
    } else if (l->getType()->isIntegerTy()) {
        switch (e.op) {
        case BinaryOp::Add: return builder_.CreateAdd(l, r);
        case BinaryOp::Sub: return builder_.CreateSub(l, r);
        case BinaryOp::Mul: return builder_.CreateMul(l, r);
        case BinaryOp::Div: return builder_.CreateSDiv(l, r);
        case BinaryOp::Eq:  return builder_.CreateICmpEQ(l, r);
        case BinaryOp::Ne:  return builder_.CreateICmpNE(l, r);
        case BinaryOp::Lt:  return builder_.CreateICmpSLT(l, r);
        case BinaryOp::Le:  return builder_.CreateICmpSLE(l, r);
        case BinaryOp::Gt:  return builder_.CreateICmpSGT(l, r);
        case BinaryOp::Ge:  return builder_.CreateICmpSGE(l, r);
        }
    }
    return fail("unsupported operand type", e.offset);
}

Value* CodeGen::genCall(const CallExpr& e) {
    if (e.callee->kind != ExprKind::Ident)
        return fail("can only call named functions", e.offset);
    auto& name = static_cast<const IdentExpr&>(*e.callee).name;

    if (name == "print") return genPrint(e);

    Function* fn = module_->getFunction(name);
    if (!fn) return fail("undefined function '" + name + "'", e.offset);
    if (fn->arg_size() != e.args.size()) return fail("wrong argument count", e.offset);

    std::vector<Value*> args;
    for (auto& a : e.args) {
        Value* v = genExpr(*a);
        if (!v) return nullptr;
        args.push_back(v);
    }
    return builder_.CreateCall(fn, args);
}

// Built-in print(x): picks a printf format from the value's type.
Value* CodeGen::genPrint(const CallExpr& e) {
    if (e.args.size() != 1) return fail("print takes one argument", e.offset);
    Value* v = genExpr(*e.args[0]);
    if (!v) return nullptr;

    const char* fmt;
    if (v->getType()->isDoubleTy())        fmt = "%f\n";
    else if (v->getType()->isPointerTy())  fmt = "%s\n";
    else if (v->getType()->isIntegerTy()) {
        fmt = "%lld\n";
        v = builder_.CreateZExtOrTrunc(v, builder_.getInt64Ty());   // bool -> i64
    } else return fail("cannot print this type", e.offset);

    auto* printfTy = FunctionType::get(builder_.getInt32Ty(), {builder_.getPtrTy()}, true);
    FunctionCallee printfFn = module_->getOrInsertFunction("printf", printfTy);
    return builder_.CreateCall(printfFn, {builder_.CreateGlobalString(fmt), v});
}

// ---------- Statements ----------

bool CodeGen::genStmt(const Stmt& s) {
    if (terminated()) return true;   // dead code after return

    switch (s.kind) {
    case StmtKind::VarDecl: {
        auto& d = static_cast<const VarDecl&>(s);
        Value* init = genExpr(*d.init);
        if (!init) return false;
        AllocaInst* slot = createEntryAlloca(init->getType(), d.name);
        builder_.CreateStore(init, slot);
        scopes_.back()[d.name] = {slot, init->getType()};
        return true;
    }
    case StmtKind::Return: {
        auto& r = static_cast<const ReturnStmt&>(s);
        if (!r.value) { builder_.CreateRet(builder_.getInt32(0)); return true; }
        Value* v = genExpr(*r.value);
        if (!v) return false;
        builder_.CreateRet(builder_.CreateZExtOrTrunc(v, builder_.getInt32Ty()));
        return true;
    }
    case StmtKind::ExprStmt:
        return genExpr(*static_cast<const ExprStmt&>(s).expr) != nullptr;
    case StmtKind::Block: {
        scopes_.emplace_back();
        bool ok = true;
        for (auto& st : static_cast<const BlockStmt&>(s).stmts)
            if (!(ok = genStmt(*st))) break;
        scopes_.pop_back();
        return ok;
    }
    case StmtKind::If: return genIf(static_cast<const IfStmt&>(s));
    default:
        fail("statement not supported yet", s.offset);
        return false;
    }
}

bool CodeGen::genIf(const IfStmt& s) {
    Value* cond = genExpr(*s.cond);
    if (!cond) return false;
    if (!cond->getType()->isIntegerTy(1)) {
        fail("condition must be a bool", s.offset);
        return false;
    }

    Function* fn = builder_.GetInsertBlock()->getParent();
    auto* thenBB  = BasicBlock::Create(*ctx_, "then", fn);
    auto* elseBB  = s.elseBranch ? BasicBlock::Create(*ctx_, "else", fn) : nullptr;
    auto* mergeBB = BasicBlock::Create(*ctx_, "endif", fn);

    builder_.CreateCondBr(cond, thenBB, elseBB ? elseBB : mergeBB);

    builder_.SetInsertPoint(thenBB);
    if (!genStmt(*s.thenBranch)) return false;
    if (!terminated()) builder_.CreateBr(mergeBB);

    if (elseBB) {
        builder_.SetInsertPoint(elseBB);
        if (!genStmt(*s.elseBranch)) return false;
        if (!terminated()) builder_.CreateBr(mergeBB);
    }

    builder_.SetInsertPoint(mergeBB);
    return true;
}

bool CodeGen::terminated() {
    BasicBlock* bb = builder_.GetInsertBlock();
    return !bb->empty() && bb->back().isTerminator();
}
