#pragma once

#include <string>
#include <vector>

struct Diagnostic {
  std::string file;
  int line = 1;
  int col = 1;
  std::string severity = "error";
  std::string message;
  std::string kind = "type error";
};

enum class Tok {
  Eof,
  Identifier,
  //Literals
  Integer,
  Float,
  String,
  //BLOCKS
  Module,
  Class,
  Trait,
  Enum,
  Function,
  Struct,
  Data,
  Implements,
  Extends,
  For,
  In,
  Super,
  Import,
  From,
  As,
  Pub,
  Abstract,
  Final,
  Private,
  Protected,
  Variable,
  Constant,
  Signal,
  //STATEMENTS
  Return,
  Pass,
  Continue,
  Break,
  If,
  Elif,
  Else,
  Switch,
  Match,
  While,
  Try,
  Do,
  Throws,
  Throw,
  Catch,
  True,
  False,
  //SYMBOLS
  At,
  Colon,
  Dot,
  Comma,
  Semi,
  LParen,
  RParen,
  LBrace,
  RBrace,
  LBracket,
  RBracket,
  LArrow,
  RArrow,
  LtEq,
  GtEq,
  DotDot,
  DotDotEq,
  //OPERATORS
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  Eq,
  EqEq,
  NotEq,
  PlusEq,
  MinusEq,
  StarEq,
  SlashEq,
  AndAnd,
  OrOr,
  Bang
};

struct Token {
  Tok kind = Tok::Eof;
  std::string text;
  long long number = 0;
  int line = 1;
  int col = 1;
  double real = 0;
};

std::vector<Token> tokenize(const std::string &source,
                            const std::string &file = "",
                            std::vector<Diagnostic> *errors = nullptr);

inline std::string locatedError(const char *kind, const std::string &file,
                                int line, int col, const std::string &msg) {
  std::string s = kind;
  if (!file.empty()) {
    s += " in ";
    s += file;
  }
  if (line > 0) {
    s += " at ";
    s += std::to_string(line);
    s += ":";
    s += std::to_string(col);
  }
  s += ": ";
  s += msg;
  return s;
}

inline std::string diagnosticError(const Diagnostic &d,
                                   const std::string &fallbackFile = "") {
  const char *kind = d.kind.empty() ? "error" : d.kind.c_str();
  return locatedError(kind, d.file.empty() ? fallbackFile : d.file, d.line,
                      d.col, d.message);
}
