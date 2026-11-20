#pragma once
#include "lexer.h"
#include "ast.h"
#include <string>
#include <vector>

class Parser {
public:
    explicit Parser(std::string_view src) : lexer_(src) { advance(); }

    Program parseProgram();
    StmtPtr parseStatement();
    ExprPtr parseExpression() { return parseExpr(0); }

    const std::vector<Lexer::Diagnostic>& lexDiagnostics() const { return lexer_.diagnostics(); }
    const std::vector<std::string>& errors() const { return errors_; }

private:
    Lexer lexer_;
    Token cur_{};                       // current token (one-token lookahead)
    std::vector<std::string> errors_;

    void advance() { cur_ = lexer_.next(); }
    bool check(Tok k) const { return cur_.kind == k; }
    bool accept(Tok k) { if (!check(k)) return false; advance(); return true; }
    Token expect(Tok k, const char* msg);
    void error(const char* msg);
    void synchronize();

    ExprPtr parseExpr(int minPrec);
    ExprPtr parsePrefix();
    StmtPtr parseVarDecl(bool isConst);
    StmtPtr parseReturn();
    StmtPtr parseIf();
    StmtPtr parseFunction();
    StmtPtr parseWhile();
    std::unique_ptr<BlockStmt> parseBlock();
};

void dump(const Expr &e, int indent = 0);
void dumpStmt(const Stmt &s, int indent = 0);