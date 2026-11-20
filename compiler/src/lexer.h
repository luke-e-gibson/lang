#pragma once
#include <string_view>
#include <cstdint>
#include <cctype>
#include <vector>
#include <string>

enum class Tok : uint8_t {
    Eof, Error,
    Ident, Int, Float, String,
    // keywords
    KwFn, KwConst, KwLet, KwReturn, KwIf, KwElse, KwTrue, KwFalse,
    // punctuation / operators
    LParen, RParen, LBrace, RBrace, Comma, Semi, Colon,
    Plus, Minus, Star, Slash,
    Assign, EqEq, NotEq, Lt, Le, Gt, Ge, Arrow,
};

struct Token {
    Tok kind;
    std::string_view text;   // slice of the source
    uint32_t offset;         // byte offset of start
};

class Lexer {
public:
    struct Diagnostic { uint32_t offset; std::string message; };
    explicit Lexer(std::string_view src) : src_(src) {}

    Token next();
    const std::vector<Diagnostic>& diagnostics() const { return diags_; }

private:
    std::vector<Diagnostic> diags_;   // in Lexer

    std::string_view src_;
    size_t pos_ = 0;

    char peek(size_t n = 0) const { return pos_ + n < src_.size() ? src_[pos_ + n] : '\0'; }
    Token make(Tok k, size_t start) const { return {k, src_.substr(start, pos_ - start), static_cast<uint32_t>(start)}; }
    
    bool match(char expected);
    static bool isIdentStart(char c) { return isalpha((unsigned char)c) || c == '_'; }
    static bool isIdentCont(char c)  { return isalnum((unsigned char)c) || c == '_'; }

    void skipTrivia();
    void skipBlockComment();

    Token identifier(size_t start);
    Token number(size_t start);
    Token string(size_t start);

    void report(size_t at, const char* msg) {
        diags_.push_back({static_cast<uint32_t>(at), msg});
    }
};