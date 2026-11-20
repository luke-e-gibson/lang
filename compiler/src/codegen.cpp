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

llvm::Type* CodeGen::lowerType(const std::string& n) {
    if (n.empty() || n == "void") return builder_.getVoidTy();
    if (n == "int" || n == "i64")  return builder_.getInt64Ty();
    if (n == "i32")                return builder_.getInt32Ty();
    if (n == "bool")               return builder_.getInt1Ty();
    if (n == "f64" || n == "float")return builder_.getDoubleTy();
    if (n == "string")             return builder_.getPtrTy();
    return nullptr;                // unknown type
}

// Lenient conversion until you have a real type checker.
llvm::Value* CodeGen::coerce(llvm::Value* v, llvm::Type* to) {
    if (v->getType() == to) return v;
    if (v->getType()->isIntegerTy() && to->isIntegerTy())
        return builder_.CreateIntCast(v, to, /*isSigned=*/true);
    return nullptr;
}

bool CodeGen::generate(const Program& prog) {
    // Pass 1: declare
    bool hasMain = false;
    for (auto& s : prog.items) {
        if (s->kind != StmtKind::Function) continue;
        auto& f = static_cast<const FunctionDecl&>(*s);
        if (module_->getFunction(f.name)) { fail("duplicate function '" + f.name + "'", f.offset); continue; }

        std::vector<llvm::Type*> ptys;
        for (auto& p : f.params) {
            llvm::Type* t = lowerType(p.typeName);
            if (!t || t->isVoidTy()) { fail("unknown type '" + p.typeName + "'", f.offset); t = builder_.getInt64Ty(); }
            ptys.push_back(t);
        }
        llvm::Type* rt = lowerType(f.returnType);
        if (!rt) { fail("unknown return type '" + f.returnType + "'", f.offset); rt = builder_.getVoidTy(); }
        if (f.name == "main") { hasMain = true; if (rt->isVoidTy()) rt = builder_.getInt32Ty(); }

        auto* fty = llvm::FunctionType::get(rt, ptys, false);
        auto* fn = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, f.name, *module_);
        unsigned i = 0;
        for (auto& a : fn->args()) a.setName(f.params[i++].name);
    }
    if (!errors_.empty()) return false;

    // Pass 2: function bodies
    for (auto& s : prog.items)
        if (s->kind == StmtKind::Function) genFunction(static_cast<const FunctionDecl&>(*s));

    // Pass 3: loose top-level statements
    bool loose = false;
    for (auto& s : prog.items) if (s->kind != StmtKind::Function) loose = true;
    if (loose) {
        if (hasMain) { fail("top-level statements cannot be mixed with 'fn main'", 0); return false; }
        auto* fty = llvm::FunctionType::get(builder_.getInt32Ty(), false);
        auto* fn = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, "main", *module_);
        currentFn_ = fn;
        builder_.SetInsertPoint(llvm::BasicBlock::Create(*ctx_, "entry", fn));
        scopes_.emplace_back();
        for (auto& s : prog.items)
            if (s->kind != StmtKind::Function && !genStmt(*s)) break;
        scopes_.pop_back();
        if (errors_.empty() && !terminated()) builder_.CreateRet(builder_.getInt32(0));
    }

    if (!errors_.empty()) return false;
    if (llvm::verifyModule(*module_, &llvm::errs())) {
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

    std::vector<llvm::Value*> args;
    unsigned i = 0;
    for (auto& a : e.args) {
        llvm::Value* v = genExpr(*a);
        if (!v) return nullptr;
        v = coerce(v, fn->getFunctionType()->getParamType(i++));
        if (!v) return fail("argument type mismatch", a->offset);
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
        if (init->getType()->isVoidTy()) { fail("cannot assign a void value", s.offset); return false; }
        AllocaInst* slot = createEntryAlloca(init->getType(), d.name);
        builder_.CreateStore(init, slot);
        scopes_.back()[d.name] = {slot, init->getType(), d.isConst};
        return true;
    }
    case StmtKind::Return: {
        auto& r = static_cast<const ReturnStmt&>(s);
        llvm::Type* rt = currentFn_->getReturnType();
        if (rt->isVoidTy()) {
            if (r.value) { fail("void function cannot return a value", s.offset); return false; }
            builder_.CreateRetVoid();
            return true;
        }
        if (!r.value) { fail("missing return value", s.offset); return false; }
        llvm::Value* v = genExpr(*r.value);
        if (!v) return false;
        v = coerce(v, rt);
        if (!v) { fail("return type mismatch", s.offset); return false; }
        builder_.CreateRet(v);
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
    case StmtKind::Assign: {
        auto& a = static_cast<const AssignStmt&>(s);
        Var* v = lookup(a.name);
        if (!v) { fail("undefined variable '" + a.name + "'", s.offset); return false; }
        if (v->isConst) {
            fail("cannot assign to const variable '" + a.name + "'", s.offset);
            return false;
        } Value* val = genExpr(*a.value);
        if (!val) return false;
        val = coerce(val, v->type);
        if (!val) { fail("type mismatch in assignment", s.offset); return false; }
        builder_.CreateStore(val, v->slot);
        return true;
    }
    case StmtKind::While: {
        auto& w = static_cast<const WhileStmt&>(s);
        Function* fn = builder_.GetInsertBlock()->getParent();
        auto* condBB = BasicBlock::Create(*ctx_, "while.cond", fn);
        auto* bodyBB = BasicBlock::Create(*ctx_, "while.body", fn);
        auto* endBB  = BasicBlock::Create(*ctx_, "while.end", fn);

        builder_.CreateBr(condBB);
        builder_.SetInsertPoint(condBB);
        Value* c = genExpr(*w.cond);
        if (!c) return false;
        builder_.CreateCondBr(c, bodyBB, endBB);

        builder_.SetInsertPoint(bodyBB);
        if (!genStmt(*w.body)) return false;
        if (!terminated()) builder_.CreateBr(condBB);

        builder_.SetInsertPoint(endBB);
        return true;
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

void CodeGen::genFunction(const FunctionDecl& f) {
    llvm::Function* fn = module_->getFunction(f.name);
    currentFn_ = fn;
    builder_.SetInsertPoint(llvm::BasicBlock::Create(*ctx_, "entry", fn));

    scopes_.emplace_back();
    for (auto& arg : fn->args()) {                    // each parameter gets a stack slot
        auto* slot = createEntryAlloca(arg.getType(), std::string(arg.getName()));
        builder_.CreateStore(&arg, slot);
        scopes_.back()[std::string(arg.getName())] = {slot, arg.getType()};
    }

    genStmt(*f.body);
    scopes_.pop_back();

    if (!errors_.empty()) return;
    if (!terminated()) {
        llvm::Type* rt = fn->getReturnType();
        if (rt->isVoidTy())                  builder_.CreateRetVoid();
        else if (f.name == "main")           builder_.CreateRet(llvm::ConstantInt::get(rt, 0));
        else                                 builder_.CreateUnreachable();   // see note below
    }
}

bool CodeGen::terminated() {
    BasicBlock* bb = builder_.GetInsertBlock();
    return !bb->empty() && bb->back().isTerminator();
}

