#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct Type;  // defined later by semantic analysis

// ---------- Expressions ----------

enum class ExprKind { IntLit, FloatLit, BoolLit, StringLit, Ident, Unary, Binary, Call };

struct Expr {
    ExprKind kind;
    uint32_t offset;
    const Type* type = nullptr;   // filled in by the type checker

    Expr(ExprKind k, uint32_t off) : kind(k), offset(off) {}
    virtual ~Expr() = default;
};
using ExprPtr = std::unique_ptr<Expr>;

struct IntLit : Expr {
    int64_t value;
    IntLit(int64_t v, uint32_t off) : Expr(ExprKind::IntLit, off), value(v) {}
};

struct FloatLit : Expr {
    double value;
    FloatLit(double v, uint32_t off) : Expr(ExprKind::FloatLit, off), value(v) {}
};

struct BoolLit : Expr {
    bool value;
    BoolLit(bool v, uint32_t off) : Expr(ExprKind::BoolLit, off), value(v) {}
};

struct StringLit : Expr {
    std::string value;            // escapes already processed
    StringLit(std::string v, uint32_t off) : Expr(ExprKind::StringLit, off), value(std::move(v)) {}
};

struct IdentExpr : Expr {
    std::string name;
    IdentExpr(std::string n, uint32_t off) : Expr(ExprKind::Ident, off), name(std::move(n)) {}
};

enum class UnaryOp { Neg, Not };
struct UnaryExpr : Expr {
    UnaryOp op;
    ExprPtr operand;
    UnaryExpr(UnaryOp o, ExprPtr e, uint32_t off)
        : Expr(ExprKind::Unary, off), op(o), operand(std::move(e)) {}
};

enum class BinaryOp { Add, Sub, Mul, Div, Eq, Ne, Lt, Le, Gt, Ge };
struct BinaryExpr : Expr {
    BinaryOp op;
    ExprPtr lhs, rhs;
    BinaryExpr(BinaryOp o, ExprPtr l, ExprPtr r, uint32_t off)
        : Expr(ExprKind::Binary, off), op(o), lhs(std::move(l)), rhs(std::move(r)) {}
};

struct CallExpr : Expr {
    ExprPtr callee;
    std::vector<ExprPtr> args;
    CallExpr(ExprPtr c, std::vector<ExprPtr> a, uint32_t off)
        : Expr(ExprKind::Call, off), callee(std::move(c)), args(std::move(a)) {}
};

// ---------- Statements ----------

enum class StmtKind { VarDecl, Assign, Return, If, While, Block, ExprStmt, Function };

struct Stmt {
    StmtKind kind;
    uint32_t offset;
    Stmt(StmtKind k, uint32_t off) : kind(k), offset(off) {}
    virtual ~Stmt() = default;
};
using StmtPtr = std::unique_ptr<Stmt>;

struct VarDecl : Stmt {
    bool isConst;
    std::string name;
    std::string typeName;         // empty = inferred
    ExprPtr init;
    VarDecl(bool c, std::string n, std::string t, ExprPtr i, uint32_t off)
        : Stmt(StmtKind::VarDecl, off), isConst(c), name(std::move(n)),
          typeName(std::move(t)), init(std::move(i)) {}
};

struct ReturnStmt : Stmt {
    ExprPtr value;                // may be null
    ReturnStmt(ExprPtr v, uint32_t off) : Stmt(StmtKind::Return, off), value(std::move(v)) {}
};

struct BlockStmt : Stmt {
    std::vector<StmtPtr> stmts;
    BlockStmt(std::vector<StmtPtr> s, uint32_t off)
        : Stmt(StmtKind::Block, off), stmts(std::move(s)) {}
};

struct IfStmt : Stmt {
    ExprPtr cond;
    StmtPtr thenBranch;
    StmtPtr elseBranch;           // may be null
    IfStmt(ExprPtr c, StmtPtr t, StmtPtr e, uint32_t off)
        : Stmt(StmtKind::If, off), cond(std::move(c)),
          thenBranch(std::move(t)), elseBranch(std::move(e)) {}
};

struct ExprStmt : Stmt {
    ExprPtr expr;
    ExprStmt(ExprPtr e, uint32_t off) : Stmt(StmtKind::ExprStmt, off), expr(std::move(e)) {}
};

struct Param { std::string name; std::string typeName; };

struct FunctionDecl : Stmt {
    std::string name;
    std::vector<Param> params;
    std::string returnType;
    std::unique_ptr<BlockStmt> body;
    FunctionDecl(std::string n, std::vector<Param> p, std::string r,
                 std::unique_ptr<BlockStmt> b, uint32_t off)
        : Stmt(StmtKind::Function, off), name(std::move(n)), params(std::move(p)),
          returnType(std::move(r)), body(std::move(b)) {}
};

struct AssignStmt : Stmt {
    std::string name;
    ExprPtr value;
    AssignStmt(std::string n, ExprPtr v, uint32_t off)
        : Stmt(StmtKind::Assign, off), name(std::move(n)), value(std::move(v)) {}
};

struct WhileStmt : Stmt {
    ExprPtr cond;
    StmtPtr body;
    WhileStmt(ExprPtr c, StmtPtr b, uint32_t off)
        : Stmt(StmtKind::While, off), cond(std::move(c)), body(std::move(b)) {}
};

struct Program {
    std::vector<StmtPtr> items;
};