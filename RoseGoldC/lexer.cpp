#include "lexer.h"

#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Lexer {
  std::string src;
  std::string file;
  size_t i = 0;
  int line = 1;
  int col = 1;

  explicit Lexer(std::string s, std::string f)
      : src(std::move(s)), file(std::move(f)) {}

  [[noreturn]] void error(const std::string &msg, int atLine, int atCol) const {
    throw std::runtime_error(
        locatedError("parse error", file, atLine, atCol, msg));
  }

  char peek(size_t off = 0) const {
    if (i + off >= src.size())
      return '\0';
    return src[i + off];
  }

  char advance() {
    char c = src[i++];
    if (c == '\n') {
      ++line;
      col = 1;
    } else {
      ++col;
    }
    return c;
  }

  void skip() {
    while (true) {
      char c = peek();
      if (std::isspace(static_cast<unsigned char>(c))) {
        advance();
        continue;
      }
      if (c == '/' && peek(1) == '/') {
        while (peek() != '\0' && peek() != '\n')
          advance();
        continue;
      }
      if (c == '/' && peek(1) == '#') {
        const int startLine = line;
        const int startCol = col;
        advance();
        advance();
        while (true) {
          if (peek() == '\0')
            error("unterminated block comment", startLine, startCol);
          if (peek() == '#' && peek(1) == '/') {
            advance();
            advance();
            break;
          }
          advance();
        }
        continue;
      }
      if (c == '#') {
        while (peek() != '\0' && peek() != '\n')
          advance();
        continue;
      }
      break;
    }
  }

  Token identOrKw(int startLine, int startCol) {
    std::string text;
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')
      text.push_back(advance());

    Token t;
    t.text = text;
    t.line = startLine;
    t.col = startCol;
    if (text == "fn")
      t.kind = Tok::Function;
    else if (text == "import")
      t.kind = Tok::Import;
    else if (text == "from")
      t.kind = Tok::From;
    else if (text == "as")
      t.kind = Tok::As;
    else if (text == "pub")
      t.kind = Tok::Pub;
    else if (text == "mod")
      t.kind = Tok::Module;
    else if (text == "class")
      t.kind = Tok::Class;
    else if (text == "trait")
      t.kind = Tok::Trait;
    else if (text == "extends")
      t.kind = Tok::Extends;
    else if (text == "for")
      t.kind = Tok::For;
    else if (text == "in")
      t.kind = Tok::In;
    else if (text == "super")
      t.kind = Tok::Super;
    else if (text == "enum")
      t.kind = Tok::Enum;
    else if (text == "match")
      t.kind = Tok::Match;
    else if (text == "switch")
      t.kind = Tok::Switch;
    else if (text == "var")
      t.kind = Tok::Variable;
    else if (text == "const")
      t.kind = Tok::Constant;
    else if (text == "struct")
      t.kind = Tok::Struct;
    else if (text == "impl")
      t.kind = Tok::Implements;
    else if (text == "signal")
      t.kind = Tok::Signal;
    else if (text == "return")
      t.kind = Tok::Return;
    else if (text == "pass")
      t.kind = Tok::Pass;
    else if (text == "break")
      t.kind = Tok::Break;
    else if (text == "continue")
      t.kind = Tok::Continue;
    else if (text == "if")
      t.kind = Tok::If;
    else if (text == "elif")
      t.kind = Tok::Elif;
    else if (text == "else")
      t.kind = Tok::Else;
    else if (text == "while")
      t.kind = Tok::While;
    else if (text == "true")
      t.kind = Tok::True;
    else if (text == "false")
      t.kind = Tok::False;
    else
      t.kind = Tok::Identifier;
    return t;
  }

  Token stringLit(int startLine, int startCol) {
    advance();
    std::string text;
    while (true) {
      char c = peek();
      if (c == '\0')
        error("unterminated string", startLine, startCol);
      if (c == '"') {
        advance();
        break;
      }
      if (c == '\\') {
        advance();
        char e = peek();
        if (e == '\0')
          error("unterminated string", startLine, startCol);
        advance();
        if (e == 'n')
          text.push_back('\n');
        else if (e == 't')
          text.push_back('\t');
        else
          text.push_back(e);
      } else {
        text.push_back(advance());
      }
    }
    Token t;
    t.kind = Tok::String;
    t.text = std::move(text);
    t.line = startLine;
    t.col = startCol;
    return t;
  }

  Token next() {
    skip();
    int startLine = line;
    int startCol = col;
    char c = peek();
    if (c == '\0')
      return {Tok::Eof, "", 0, startLine, startCol};

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
      return identOrKw(startLine, startCol);

    if (std::isdigit(static_cast<unsigned char>(c))) {
      std::string digits;
      while (std::isdigit(static_cast<unsigned char>(peek())))
        digits.push_back(advance());
      if (peek() == '.' &&
          std::isdigit(static_cast<unsigned char>(peek(1)))) {
        digits.push_back(advance());
        while (std::isdigit(static_cast<unsigned char>(peek())))
          digits.push_back(advance());
        Token t;
        t.kind = Tok::Float;
        t.text = digits;
        t.real = std::stod(digits);
        t.line = startLine;
        t.col = startCol;
        return t;
      }
      Token t;
      t.kind = Tok::Integer;
      t.text = digits;
      t.number = std::stoll(digits);
      t.line = startLine;
      t.col = startCol;
      return t;
    }

    if (c == '"')
      return stringLit(startLine, startCol);

    auto make = [&](Tok k, const std::string &text) {
      Token t;
      t.kind = k;
      t.text = text;
      t.line = startLine;
      t.col = startCol;
      return t;
    };

    advance();
    char n = peek();
    if (c == '=' && n == '=') {
      advance();
      return make(Tok::EqEq, "==");
    }
    if (c == '!' && n == '=') {
      advance();
      return make(Tok::NotEq, "!=");
    }

    switch (c) {
    case '@':
      return make(Tok::At, "@");
    case ':':
      return make(Tok::Colon, ":");
    case '.':
      return make(Tok::Dot, ".");
    case ',':
      return make(Tok::Comma, ",");
    case ';':
      return make(Tok::Semi, ";");
    case '(':
      return make(Tok::LParen, "(");
    case ')':
      return make(Tok::RParen, ")");
    case '{':
      return make(Tok::LBrace, "{");
    case '}':
      return make(Tok::RBrace, "}");
    case '[':
      return make(Tok::LBracket, "[");
    case ']':
      return make(Tok::RBracket, "]");
    case '+':
      return make(Tok::Plus, "+");
    case '-':
      return make(Tok::Minus, "-");
    case '*':
      return make(Tok::Star, "*");
    case '/':
      return make(Tok::Slash, "/");
    case '%':
      return make(Tok::Percent, "%");
    case '=':
      return make(Tok::Eq, "=");
    case '<':
      return make(Tok::LArrow, "<");
    case '>':
      return make(Tok::RArrow, ">");
    case '!':
      return make(Tok::Bang, "!");
    default:
      error(std::string("unexpected character '") + c + "'", startLine,
            startCol);
    }
  }
};

} // namespace

std::vector<Token> tokenize(const std::string &source, const std::string &file) {
  Lexer lexer(source, file);
  std::vector<Token> tokens;
  while (true) {
    Token t = lexer.next();
    const bool done = t.kind == Tok::Eof;
    tokens.push_back(std::move(t));
    if (done)
      break;
  }
  return tokens;
}
