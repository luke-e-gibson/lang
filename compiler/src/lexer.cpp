#include "lexer.h"
#include <unordered_map>

const char* tokName(Tok t) {
  switch (t) {
        case Tok::Eof: return "Eof";
        case Tok::Error: return "Error";
        case Tok::Ident: return "Ident";
        case Tok::Int: return "Int";
        case Tok::Float: return "Float";
        case Tok::String: return "String";
        case Tok::KwFn: return "KwFn";
        case Tok::KwConst: return "KwConst";
        case Tok::KwLet: return "KwLet";
        case Tok::KwReturn: return "KwReturn";
        case Tok::KwIf: return "KwIf";
        case Tok::KwElse: return "KwElse";
        case Tok::KwTrue: return "KwTrue";
        case Tok::KwFalse: return "KwFalse";
        case Tok::KwWhile: return "KwWhile";
        case Tok::LParen: return "LParen";
        case Tok::RParen: return "RParen";
        case Tok::LBrace: return "LBrace";
        case Tok::RBrace: return "RBrace";
        case Tok::Comma: return "Comma";
        case Tok::Semi: return "Semi";
        case Tok::Colon: return "Colon";
        case Tok::Plus: return "Plus";
        case Tok::Minus: return "Minus";
        case Tok::Star: return "Star";
        case Tok::Slash: return "Slash";
        case Tok::Assign: return "Assign";
        case Tok::EqEq: return "EqEq";
        case Tok::NotEq: return "NotEq";
        case Tok::Lt: return "Lt";
        case Tok::Le: return "Le";
        case Tok::Gt: return "Gt";
        case Tok::Ge: return "Ge";
        case Tok::Arrow: return "Arrow";
        default: return "?";
    }
}

Token Lexer::next() {
    skipTrivia();
    if (pos_ >= src_.size()) return make(Tok::Eof, pos_);

    size_t start = pos_;
    char c = src_[pos_];

    if (isIdentStart(c)) return identifier(start);
    if (isdigit((unsigned char)c)) return number(start);
    if (c == '"') return string(start);

    ++pos_;
    switch (c) {
        case '(': return make(Tok::LParen, start);
        case ')': return make(Tok::RParen, start);
        case '{': return make(Tok::LBrace, start);
        case '}': return make(Tok::RBrace, start);
        case ',': return make(Tok::Comma, start);
        case ';': return make(Tok::Semi, start);
        case ':': return make(Tok::Colon, start);
        case '+': return make(Tok::Plus, start);
        case '*': return make(Tok::Star, start);
        case '/': return make(Tok::Slash, start);
        case '-': return match('>') ? make(Tok::Arrow, start) : make(Tok::Minus, start);
        case '=': return match('=') ? make(Tok::EqEq, start)  : make(Tok::Assign, start);
        case '!': if (match('=')) return make(Tok::NotEq, start); break;
        case '<': return match('=') ? make(Tok::Le, start) : make(Tok::Lt, start);
        case '>': return match('=') ? make(Tok::Ge, start) : make(Tok::Gt, start);
    }
    
    report(start, "Unknown character");
    return make(Tok::Error, start);  // unknown character
}

bool Lexer::match(char expected) {
  if (peek() != expected)
    return false;
  ++pos_;
  return true;
}

void Lexer::skipTrivia() {
  for (;;) {
    char c = peek();
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      ++pos_;
    } else if (c == '/' && peek(1) == '/') {
      while (peek() && peek() != '\n')
        ++pos_;
    } else if (c == '/' && peek(1) == '*') {
      skipBlockComment();
    } else
      break;
  }
}

void Lexer::skipBlockComment() {
  size_t start = pos_;
  pos_ += 2;
  while (pos_ < src_.size() && !(peek() == '*' && peek(1) == '/'))
    ++pos_;
  if (pos_ >= src_.size())
    report(start, "Unterminated block comment");
  else
    pos_ += 2;
}

Token Lexer::identifier(size_t start) {
  while (isIdentCont(peek()))
    ++pos_;
  auto text = src_.substr(start, pos_ - start);
  static const std::unordered_map<std::string_view, Tok> keywords = {
      {"fn", Tok::KwFn}, {"const", Tok::KwConst}, {"let", Tok::KwLet}, {"return", Tok::KwReturn}, {"while", Tok::KwWhile},
      {"if", Tok::KwIf}, {"else", Tok::KwElse},
      {"true", Tok::KwTrue}, {"false", Tok::KwFalse},
  };
  auto it = keywords.find(text);
  return make(it != keywords.end() ? it->second : Tok::Ident, start);
}

Token Lexer::number(size_t start) {
  while (isdigit((unsigned char)peek()))
    ++pos_;
  bool isFloat = false;
  if (peek() == '.' && isdigit((unsigned char)peek(1))) {
    isFloat = true;
    ++pos_;
    while (isdigit((unsigned char)peek()))
      ++pos_;
  }
  return make(isFloat ? Tok::Float : Tok::Int, start);
}

Token Lexer::string(size_t start) {
  ++pos_; // opening quote
  while (pos_ < src_.size() && peek() != '"') {
    if (peek() == '\\')
      ++pos_; // skip escaped char
    ++pos_;
  }
  if (pos_ >= src_.size()) {
    report(start, "Unterminated string literal");
    return make(Tok::Error, start); // unterminated
  }
  ++pos_;                           // closing quote
  return make(Tok::String, start);
}
