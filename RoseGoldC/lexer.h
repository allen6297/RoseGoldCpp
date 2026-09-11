#pragma once

#include <string>
#include <vector>

enum class Tok {
  Eof,
  Identifier,
  //Literals
  Integer,
  String,
  //BLOCKS
  Module,
  Class,
  Trait,
  Function,
  Struct,
  Implements,
  Extends,
  For,
  Super,
  Import,
  From,
  As,
  Pub,
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
  LArrow,
  RArrow,
  //OPERATORS
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  Eq,
  EqEq,
  NotEq,
  Bang
};

struct Token {
  Tok kind = Tok::Eof;
  std::string text;
  long long number = 0;
  int line = 1;
  int col = 1;
};

std::vector<Token> tokenize(const std::string &source,
                            const std::string &file = "");

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
