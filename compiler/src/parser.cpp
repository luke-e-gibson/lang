#include "parser.h"

void Parser::error(const char* msg) {
    errors_.push_back("error at " + std::to_string(cur_.offset) + ": " + msg);
}

Token Parser::expect(Tok k, const char* msg) {
    Token t = cur_;
    if (!check(k)) { error(msg); return t; }   // don't consume on failure
    advance();
    return t;
}

// Error recovery: skip to a likely statement boundary
void Parser::synchronize() {
    while (!check(Tok::Eof)) {
        if (accept(Tok::Semi)) return;
        switch (cur_.kind) {
            case Tok::KwConst: case Tok::KwLet: case Tok::KwFn:
            case Tok::KwReturn: case Tok::KwIf: case Tok::RBrace:
                return;
            default: advance();
        }
    }
}

struct OpInfo { BinaryOp op; int prec; };

static bool binaryOpInfo(Tok t, OpInfo& out) {
    switch (t) {
        case Tok::EqEq:  out = {BinaryOp::Eq, 1}; return true;
        case Tok::NotEq: out = {BinaryOp::Ne, 1}; return true;
        case Tok::Lt:    out = {BinaryOp::Lt, 2}; return true;
        case Tok::Le:    out = {BinaryOp::Le, 2}; return true;
        case Tok::Gt:    out = {BinaryOp::Gt, 2}; return true;
        case Tok::Ge:    out = {BinaryOp::Ge, 2}; return true;
        case Tok::Plus:  out = {BinaryOp::Add, 3}; return true;
        case Tok::Minus: out = {BinaryOp::Sub, 3}; return true;
        case Tok::Star:  out = {BinaryOp::Mul, 4}; return true;
        case Tok::Slash: out = {BinaryOp::Div, 4}; return true;
        default: return false;
    }
}

ExprPtr Parser::parseExpr(int minPrec) {
    ExprPtr lhs = parsePrefix();
    if (!lhs) return nullptr;

    for (;;) {
        OpInfo info;
        if (!binaryOpInfo(cur_.kind, info) || info.prec < minPrec) break;

        uint32_t off = cur_.offset;
        advance();                                   // consume operator
        ExprPtr rhs = parseExpr(info.prec + 1);      // +1 => left-associative
        if (!rhs) return nullptr;
        lhs = std::make_unique<BinaryExpr>(info.op, std::move(lhs), std::move(rhs), off);
    }
    return lhs;
}

ExprPtr Parser::parsePrefix() {
    Token t = cur_;
    switch (t.kind) {
        case Tok::Int: {
            advance();
            int64_t v = std::stoll(std::string(t.text));   // wrap in try/catch for overflow
            return std::make_unique<IntLit>(v, t.offset);
        }
        case Tok::Float: {
            advance();
            return std::make_unique<FloatLit>(std::stod(std::string(t.text)), t.offset);
        }
        case Tok::KwTrue:  advance(); return std::make_unique<BoolLit>(true,  t.offset);
        case Tok::KwFalse: advance(); return std::make_unique<BoolLit>(false, t.offset);
        case Tok::String: {
            advance();
            // strip quotes; process escapes here later
            std::string s(t.text.substr(1, t.text.size() - 2));
            return std::make_unique<StringLit>(std::move(s), t.offset);
        }
        case Tok::Ident: {
            advance();
            ExprPtr e = std::make_unique<IdentExpr>(std::string(t.text), t.offset);
            // call: ident ( args )
            while (check(Tok::LParen)) {
                uint32_t off = cur_.offset;
                advance();
                std::vector<ExprPtr> args;
                if (!check(Tok::RParen)) {
                    do {
                        ExprPtr a = parseExpr(0);
                        if (!a) return nullptr;
                        args.push_back(std::move(a));
                    } while (accept(Tok::Comma));
                }
                expect(Tok::RParen, "expected ')' after arguments");
                e = std::make_unique<CallExpr>(std::move(e), std::move(args), off);
            }
            return e;
        }
        case Tok::Minus: {
            advance();
            ExprPtr operand = parseExpr(5);   // binds tighter than * and /
            if (!operand) return nullptr;
            return std::make_unique<UnaryExpr>(UnaryOp::Neg, std::move(operand), t.offset);
        }
        case Tok::LParen: {
            advance();
            ExprPtr e = parseExpr(0);
            expect(Tok::RParen, "expected ')'");
            return e;
        }
        default:
            error("expected expression");
            return nullptr;
    }
}

StmtPtr Parser::parseVarDecl(bool isConst) {
    uint32_t off = cur_.offset;
    advance();                                           // const / let
    Token name = expect(Tok::Ident, "expected variable name");

    std::string typeName;
    if (accept(Tok::Colon)) {
        Token ty = expect(Tok::Ident, "expected type name");
        typeName = std::string(ty.text);
    }

    expect(Tok::Assign, "expected '='");
    ExprPtr init = parseExpr(0);
    if (!init) return nullptr;
    expect(Tok::Semi, "expected ';'");
    return std::make_unique<VarDecl>(isConst, std::string(name.text),
                                     std::move(typeName), std::move(init), off);
}

StmtPtr Parser::parseReturn() {
    uint32_t off = cur_.offset;
    advance();                                           // return
    ExprPtr value;
    if (!check(Tok::Semi)) {
        value = parseExpr(0);
        if (!value) return nullptr;
    }
    expect(Tok::Semi, "expected ';'");
    return std::make_unique<ReturnStmt>(std::move(value), off);
}

std::unique_ptr<BlockStmt> Parser::parseBlock() {
    uint32_t off = cur_.offset;
    expect(Tok::LBrace, "expected '{'");
    std::vector<StmtPtr> stmts;
    while (!check(Tok::RBrace) && !check(Tok::Eof)) {
        if (StmtPtr s = parseStatement()) stmts.push_back(std::move(s));
        else synchronize();
    }
    expect(Tok::RBrace, "expected '}'");
    return std::make_unique<BlockStmt>(std::move(stmts), off);
}

StmtPtr Parser::parseIf() {
    uint32_t off = cur_.offset;
    advance();                                   // if
    ExprPtr cond = parseExpr(0);                 // works with or without parentheses
    if (!cond) return nullptr;

    StmtPtr thenBranch = parseBlock();

    StmtPtr elseBranch;
    if (accept(Tok::KwElse)) {
        if (check(Tok::KwIf)) elseBranch = parseIf();   // else if
        else                  elseBranch = parseBlock();
    }
    return std::make_unique<IfStmt>(std::move(cond), std::move(thenBranch),
                                    std::move(elseBranch), off);
}

StmtPtr Parser::parseStatement() {
    switch (cur_.kind) {
        case Tok::KwConst: return parseVarDecl(true);
        case Tok::KwLet:   return parseVarDecl(false);
        case Tok::KwIf:    return parseIf();
        case Tok::KwReturn: return parseReturn();
        case Tok::KwFn: return parseFunction();
        case Tok::LBrace:  return parseBlock();
        case Tok::KwWhile: return parseWhile();
        default: {
            uint32_t off = cur_.offset;
            ExprPtr e = parseExpr(0);
            if (!e) return nullptr;
            if (accept(Tok::Assign)) {
                if (e->kind != ExprKind::Ident) { error("invalid assignment target"); return nullptr; }
                ExprPtr rhs = parseExpr(0);
                if (!rhs) return nullptr;
                expect(Tok::Semi, "expected ';'");
                return std::make_unique<AssignStmt>(static_cast<IdentExpr&>(*e).name, std::move(rhs), off);
            }
            expect(Tok::Semi, "expected ';'");
            return std::make_unique<ExprStmt>(std::move(e), off);
        }
    }
}

StmtPtr Parser::parseWhile() {
    uint32_t off = cur_.offset;
    advance();                                   // while
    ExprPtr cond = parseExpr(0);
    if (!cond) return nullptr;
    StmtPtr body = parseBlock();
    return std::make_unique<WhileStmt>(std::move(cond), std::move(body), off);
}

StmtPtr Parser::parseFunction() {
    uint32_t off = cur_.offset;
    advance();                                            // fn
    Token name = expect(Tok::Ident, "expected function name");

    std::vector<Param> params;
    expect(Tok::LParen, "expected '('");
    if (!check(Tok::RParen)) {
        do {
            Token pn = expect(Tok::Ident, "expected parameter name");
            expect(Tok::Colon, "expected ':' after parameter name");
            Token pt = expect(Tok::Ident, "expected parameter type");
            params.push_back({std::string(pn.text), std::string(pt.text)});
        } while (accept(Tok::Comma));
    }
    expect(Tok::RParen, "expected ')'");

    std::string ret;                                      // empty = void
    if (accept(Tok::Arrow))
        ret = std::string(expect(Tok::Ident, "expected return type").text);

    auto body = parseBlock();
    return std::make_unique<FunctionDecl>(std::string(name.text), std::move(params),
                                          std::move(ret), std::move(body), off);
}

Program Parser::parseProgram() {
    Program prog;
    while (!check(Tok::Eof)) {
        uint32_t before = cur_.offset;
        if (StmtPtr s = parseStatement()) {
            prog.items.push_back(std::move(s));
        } else {
            synchronize();
            if (!check(Tok::Eof) && cur_.offset == before) advance();  // no progress: skip a token
        }
    }
    return prog;
}

void dump(const Expr &e, int indent) {
  std::string pad(indent * 2, ' ');
  switch (e.kind) {
  case ExprKind::IntLit:
    printf("%sInt(%lld)\n", pad.c_str(),
           (long long)static_cast<const IntLit &>(e).value);
    break;
  case ExprKind::Binary: {
    auto &b = static_cast<const BinaryExpr &>(e);
    printf("%sBinary(%d)\n", pad.c_str(), (int)b.op);
    dump(*b.lhs, indent + 1);
    dump(*b.rhs, indent + 1);
    break;
  }
  case ExprKind::FloatLit:
      printf("%sFloat(%g)\n", pad.c_str(), static_cast<const FloatLit &>(e).value);
      break;
  case ExprKind::BoolLit:
    printf("%sBool(%s)\n", pad.c_str(),
           static_cast<const BoolLit &>(e).value ? "true" : "false");
    break;
  case ExprKind::StringLit:
    printf("%sString(\"%s\")\n", pad.c_str(),
           static_cast<const StringLit &>(e).value.c_str());
    break;
  case ExprKind::Ident:
    printf("%sIdent(%s)\n", pad.c_str(),
           static_cast<const IdentExpr &>(e).name.c_str());
    break;
  case ExprKind::Unary: {
    auto &u = static_cast<const UnaryExpr &>(e);
    printf("%sUnary(%d)\n", pad.c_str(), (int)u.op);
    dump(*u.operand, indent + 1);
    break;
  }
  case ExprKind::Call: {
    auto &c = static_cast<const CallExpr &>(e);
    printf("%sCall\n", pad.c_str());
    dump(*c.callee, indent + 1);
    for (auto &a : c.args) dump(*a, indent + 1);
    break;
  }
  default:
    break;
  }
}

void dumpStmt(const Stmt &s, int indent) {
  std::string pad(indent * 2, ' ');
  switch (s.kind) {
  case StmtKind::VarDecl: {
    auto &v = static_cast<const VarDecl &>(s);
    printf("%sVarDecl(%s)\n", pad.c_str(), v.name.c_str());
    dump(*v.init, indent + 1);
    break;
  }
  case StmtKind::Return: {
    auto &r = static_cast<const ReturnStmt &>(s);
    printf("%sReturn\n", pad.c_str());
    if (r.value)
      dump(*r.value, indent + 1);
    break;
  }
  case StmtKind::Block: {
    auto &b = static_cast<const BlockStmt &>(s);
    printf("%sBlock\n", pad.c_str());
    for (auto &stmt : b.stmts)
      dumpStmt(*stmt, indent + 1);
    break;
  }
  case StmtKind::If: {
    auto &i = static_cast<const IfStmt &>(s);
    printf("%sIf\n", pad.c_str());
    dump(*i.cond, indent + 1);
    dumpStmt(*i.thenBranch, indent + 1);
    if (i.elseBranch)
      dumpStmt(*i.elseBranch, indent + 1);
    break;
  }
  case StmtKind::ExprStmt: {
    auto &e = static_cast<const ExprStmt &>(s);
    printf("%sExprStmt\n", pad.c_str());
    dump(*e.expr, indent + 1);
    break;
  }
  default:
    break;
  }
}
