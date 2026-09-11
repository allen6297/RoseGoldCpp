#include "parser.h"
#include "ast.h"
#include "lexer.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct ParseError {};

struct Parser {
  std::vector<Token> tokens;
  std::string file;
  std::vector<Diagnostic> *errors = nullptr;
  size_t i = 0;

  explicit Parser(std::vector<Token> t, std::string f,
                  std::vector<Diagnostic> *e)
      : tokens(std::move(t)), file(std::move(f)), errors(e) {}

  const Token &peek() const { return tokens[i]; }
  const Token &prev() const { return tokens[i - 1]; }

  bool check(Tok k) const { return peek().kind == k; }

  bool match(Tok k) {
    if (!check(k))
      return false;
    ++i;
    return true;
  }

  const Token &advance() { return tokens[i++]; }

  void record(const std::string &msg) {
    Diagnostic d;
    d.file = file;
    d.line = peek().line > 0 ? peek().line : 1;
    d.col = peek().col > 0 ? peek().col : 1;
    d.severity = "error";
    d.message = msg;
    d.kind = "parse error";
    if (errors) {
      for (const auto &prev : *errors) {
        if (prev.file == d.file && prev.line == d.line && prev.col == d.col &&
            prev.message == d.message)
          return;
      }
      errors->push_back(std::move(d));
    }
  }

  [[noreturn]] void errorHere(const std::string &msg) {
    record(msg);
    throw ParseError();
  }

  void errorNote(const std::string &msg) { record(msg); }

  void synchronizeStmt() {
    while (!check(Tok::Eof) && !check(Tok::RBrace) && !check(Tok::Semi))
      advance();
    if (check(Tok::Semi))
      advance();
  }

  bool atItemStart() const {
    return check(Tok::Function) || check(Tok::Struct) || check(Tok::Data) ||
           check(Tok::Class) ||
           check(Tok::Trait) || check(Tok::Enum) || check(Tok::Implements) ||
           check(Tok::Signal) || check(Tok::Module) || check(Tok::Import) ||
           check(Tok::From) || check(Tok::At) || check(Tok::Pub) ||
           check(Tok::Private) || check(Tok::Protected) ||
           check(Tok::Abstract) || check(Tok::Final);
  }

  void synchronizeItem() {
    if (check(Tok::Eof))
      return;
    advance();
    while (!check(Tok::Eof)) {
      if (atItemStart())
        return;
      if (check(Tok::RBrace) || check(Tok::Semi)) {
        advance();
        return;
      }
      advance();
    }
  }

  const Token &expect(Tok k, const std::string &msg) {
    if (!check(k))
      errorHere(msg);
    return advance();
  }

  std::string parseType() {
    const Token &name = expect(Tok::Identifier, "expected type name");
    std::string t = name.text;
    if (match(Tok::LArrow)) {
      if (!check(Tok::RArrow)) {
        do {
          parseType();
        } while (match(Tok::Comma));
      }
      expect(Tok::RArrow, "expected '>'");
    }
    return t;
  }

  std::string parseOptionalType() {
    if (!match(Tok::Colon))
      return "";
    return parseType();
  }

  Expr make(Expr::Kind kind, int line, int col) {
    Expr e;
    e.kind = kind;
    e.line = line;
    e.col = col;
    return e;
  }

  struct FnAttrs {
    bool isTest = false;
    bool isDeprecated = false;
    bool isConstexpr = false;
    bool isUfcs = false;
    bool isOptional = false;
    bool any() const {
      return isTest || isDeprecated || isConstexpr || isUfcs || isOptional;
    }
    bool fnLike() const {
      return isTest || isDeprecated || isConstexpr || isUfcs;
    }
  };

  Expr parsePrimary() {
    const Token &t = peek();
    if (match(Tok::Integer)) {
      Expr e = make(Expr::Kind::Int, t.line, t.col);
      e.number = t.number;
      return e;
    }
    if (match(Tok::Float)) {
      Expr e = make(Expr::Kind::Float, t.line, t.col);
      e.real = t.real;
      return e;
    }
    if (match(Tok::String)) {
      Expr e = make(Expr::Kind::String, t.line, t.col);
      e.text = t.text;
      return e;
    }
    if (match(Tok::True) || match(Tok::False)) {
      Expr e = make(Expr::Kind::Bool, t.line, t.col);
      e.boolean = (t.kind == Tok::True);
      return e;
    }
    if (match(Tok::Identifier) || match(Tok::Super)) {
      Expr e = make(Expr::Kind::Var, t.line, t.col);
      e.text = t.kind == Tok::Super ? "super" : t.text;
      return e;
    }
    if (match(Tok::LParen)) {
      Expr inner = parseExpr();
      expect(Tok::RParen, "expected ')'");
      return inner;
    }
    if (match(Tok::LBracket)) {
      Expr e = make(Expr::Kind::Array, t.line, t.col);
      if (!check(Tok::RBracket)) {
        do {
          e.kids.push_back(parseExpr());
        } while (match(Tok::Comma));
      }
      expect(Tok::RBracket, "expected ']' after array");
      return e;
    }
    if (match(Tok::LBrace)) {
      Expr e = make(Expr::Kind::Map, t.line, t.col);
      if (!check(Tok::RBrace)) {
        while (true) {
          e.kids.push_back(parseExpr());
          expect(Tok::Colon, "expected ':' after map key");
          e.kids.push_back(parseExpr());
          if (!match(Tok::Comma))
            break;
          if (check(Tok::RBrace))
            break;
        }
      }
      expect(Tok::RBrace, "expected '}' after map");
      return e;
    }
    errorHere("expected expression");
  }

  bool isStructLitStart() const {
    if (!check(Tok::LBrace) || i + 1 >= tokens.size())
      return false;
    if (tokens[i + 1].kind == Tok::RBrace)
      return true;
    if (i + 2 >= tokens.size())
      return false;
    return tokens[i + 1].kind == Tok::Identifier &&
           tokens[i + 2].kind == Tok::Colon;
  }

  Expr parseStructLit(Expr typeName) {
    expect(Tok::LBrace, "expected '{'");
    Expr lit = make(Expr::Kind::StructLit, typeName.line, typeName.col);
    lit.text = std::move(typeName.text);
    if (!check(Tok::RBrace)) {
      while (true) {
        if (check(Tok::RBrace))
          break;
        const Token &field = expect(Tok::Identifier, "expected field name");
        expect(Tok::Colon, "expected ':' after field name");
        lit.names.push_back(field.text);
        lit.kids.push_back(parseExpr());
        if (!match(Tok::Comma))
          break;
      }
    }
    expect(Tok::RBrace, "expected '}' after struct fields");
    return lit;
  }

  // MARK:CALLS
  Expr parseCall() {
    Expr expr = parsePrimary();
    while (true) {
      if (match(Tok::Dot)) {
        const Token &name = expect(Tok::Identifier, "expected name after '.'");
        if (match(Tok::LParen)) {
          Expr call = make(Expr::Kind::MethodCall, name.line, name.col);
          call.text = name.text;
          call.kids.push_back(std::move(expr));
          if (!check(Tok::RParen)) {
            do {
              call.kids.push_back(parseExpr());
            } while (match(Tok::Comma));
          }
          expect(Tok::RParen, "expected ')'");
          expr = std::move(call);
        } else {
          Expr mem = make(Expr::Kind::Member, name.line, name.col);
          mem.text = name.text;
          mem.kids.push_back(std::move(expr));
          expr = std::move(mem);
        }
      } else if (match(Tok::LParen)) {
        if (expr.kind != Expr::Kind::Var)
          errorHere("can only call a name");
        Expr call = make(Expr::Kind::Call, expr.line, expr.col);
        call.text = expr.text;
        if (!check(Tok::RParen)) {
          do {
            call.kids.push_back(parseExpr());
          } while (match(Tok::Comma));
        }
        expect(Tok::RParen, "expected ')'");
        expr = std::move(call);
      } else if (match(Tok::LBracket)) {
        Expr idx = make(Expr::Kind::Index, expr.line, expr.col);
        idx.kids.push_back(std::move(expr));
        idx.kids.push_back(parseExpr());
        expect(Tok::RBracket, "expected ']' after index");
        expr = std::move(idx);
      } else if (expr.kind == Expr::Kind::Var && isStructLitStart()) {
        expr = parseStructLit(std::move(expr));
      } else {
        break;
      }
    }
    return expr;
  }
  // MARK: EXPRESSIONS
  Expr parseUnary() {
    if (match(Tok::Try)) {
      const Token &op = prev();
      Expr e = make(Expr::Kind::Try, op.line, op.col);
      e.kids.push_back(parseUnary());
      return e;
    }
    if (match(Tok::Minus) || match(Tok::Bang)) {
      const Token &op = prev();
      Expr e = make(Expr::Kind::Unary, op.line, op.col);
      e.text = op.text;
      e.kids.push_back(parseUnary());
      return e;
    }
    return parseCall();
  }

  Expr parseFactor() {
    Expr left = parseUnary();
    while (match(Tok::Star) || match(Tok::Slash) || match(Tok::Percent)) {
      const Token &op = prev();
      Expr e = make(Expr::Kind::Binary, op.line, op.col);
      e.text = op.text;
      e.kids.push_back(std::move(left));
      e.kids.push_back(parseUnary());
      left = std::move(e);
    }
    return left;
  }

  Expr parseTerm() {
    Expr left = parseFactor();
    while (match(Tok::Plus) || match(Tok::Minus)) {
      const Token &op = prev();
      Expr e = make(Expr::Kind::Binary, op.line, op.col);
      e.text = op.text;
      e.kids.push_back(std::move(left));
      e.kids.push_back(parseFactor());
      left = std::move(e);
    }
    return left;
  }

  Expr parseComparison() {
    Expr left = parseTerm();
    while (match(Tok::LArrow) || match(Tok::RArrow) || match(Tok::LtEq) ||
           match(Tok::GtEq)) {
      const Token &op = prev();
      Expr e = make(Expr::Kind::Binary, op.line, op.col);
      e.text = op.text;
      e.kids.push_back(std::move(left));
      e.kids.push_back(parseTerm());
      left = std::move(e);
    }
    return left;
  }

  Expr parseEquality() {
    Expr left = parseComparison();
    while (match(Tok::EqEq) || match(Tok::NotEq)) {
      const Token &op = prev();
      Expr e = make(Expr::Kind::Binary, op.line, op.col);
      e.text = op.text;
      e.kids.push_back(std::move(left));
      e.kids.push_back(parseComparison());
      left = std::move(e);
    }
    return left;
  }

  Expr parseAnd() {
    Expr left = parseEquality();
    while (match(Tok::AndAnd)) {
      const Token &op = prev();
      Expr e = make(Expr::Kind::Binary, op.line, op.col);
      e.text = op.text;
      e.kids.push_back(std::move(left));
      e.kids.push_back(parseEquality());
      left = std::move(e);
    }
    return left;
  }

  Expr parseOr() {
    Expr left = parseAnd();
    while (match(Tok::OrOr)) {
      const Token &op = prev();
      Expr e = make(Expr::Kind::Binary, op.line, op.col);
      e.text = op.text;
      e.kids.push_back(std::move(left));
      e.kids.push_back(parseAnd());
      left = std::move(e);
    }
    return left;
  }

  Expr parseRange() {
    Expr left = parseOr();
    if (match(Tok::DotDot) || match(Tok::DotDotEq)) {
      const Token &op = prev();
      Expr e = make(Expr::Kind::Range, op.line, op.col);
      e.boolean = op.kind == Tok::DotDotEq;
      e.text = op.text;
      e.kids.push_back(std::move(left));
      e.kids.push_back(parseOr());
      return e;
    }
    return left;
  }

  Expr parseExpr() { return parseRange(); }

  std::vector<Stmt> parseBlock() {
    expect(Tok::LBrace, "expected '{'");
    std::vector<Stmt> stmts;
    while (!check(Tok::RBrace) && !check(Tok::Eof)) {
      try {
        stmts.push_back(parseStmt());
      } catch (const ParseError &) {
        synchronizeStmt();
      }
    }
    expect(Tok::RBrace, "expected '}'");
    return stmts;
  }

  Stmt parseIfAt(int line, int col) {
    Stmt s;
    s.kind = Stmt::Kind::If;
    s.line = line;
    s.col = col;
    s.expr = parseExpr();
    s.body = parseBlock();
    if (match(Tok::Elif)) {
      const Token &el = prev();
      s.elseBody.push_back(parseIfAt(el.line, el.col));
    } else if (match(Tok::Else)) {
      s.elseBody = parseBlock();
    }
    return s;
  }

  Stmt parseStmt() {
    const Token &t = peek();
    if (match(Tok::If))
      return parseIfAt(t.line, t.col);
    if (match(Tok::Do)) {
      Stmt s;
      s.kind = Stmt::Kind::Do;
      s.line = t.line;
      s.col = t.col;
      s.body = parseBlock();
      if (match(Tok::Catch)) {
        s.name = expect(Tok::Identifier, "expected catch binding").text;
        s.elseBody = parseBlock();
      }
      return s;
    }
    if (match(Tok::Throw)) {
      Stmt s;
      s.kind = Stmt::Kind::Throw;
      s.line = t.line;
      s.col = t.col;
      s.expr = parseExpr();
      expect(Tok::Semi, "expected ';'");
      return s;
    }
    if (match(Tok::While)) {
      Stmt s;
      s.kind = Stmt::Kind::While;
      s.line = t.line;
      s.col = t.col;
      s.expr = parseExpr();
      s.body = parseBlock();
      return s;
    }
    if (match(Tok::For)) {
      Stmt s;
      s.kind = Stmt::Kind::For;
      s.line = t.line;
      s.col = t.col;
      s.name = expect(Tok::Identifier, "expected loop variable").text;
      expect(Tok::In, "expected 'in' after loop variable");
      s.expr = parseExpr();
      s.body = parseBlock();
      return s;
    }
    if (check(Tok::Match) || check(Tok::Switch))
      return parseMatchStmt();
    if (match(Tok::Return)) {
      Stmt s;
      s.kind = Stmt::Kind::Return;
      s.line = t.line;
      s.col = t.col;
      if (!check(Tok::Semi))
        s.expr = parseExpr();
      expect(Tok::Semi, "expected ';'");
      return s;
    }
    if (match(Tok::Pass) || match(Tok::Break) || match(Tok::Continue)) {
      Stmt s;
      if (prev().kind == Tok::Pass)
        s.kind = Stmt::Kind::Pass;
      else if (prev().kind == Tok::Break)
        s.kind = Stmt::Kind::Break;
      else
        s.kind = Stmt::Kind::Continue;
      s.line = t.line;
      s.col = t.col;
      expect(Tok::Semi, "expected ';'");
      return s;
    }
    if (match(Tok::Variable) || match(Tok::Constant)) {
      const bool isConst = prev().kind == Tok::Constant;
      const Token &name = expect(Tok::Identifier, "expected variable name");
      std::string ty = parseOptionalType();
      expect(Tok::Eq, "expected '='");
      Stmt s;
      s.kind = isConst ? Stmt::Kind::Const : Stmt::Kind::Var;
      s.name = name.text;
      s.typeName = std::move(ty);
      s.expr = parseExpr();
      s.line = name.line;
      s.col = name.col;
      expect(Tok::Semi, "expected ';'");
      return s;
    }
    auto isAssignTok = [](Tok k) {
      return k == Tok::Eq || k == Tok::PlusEq || k == Tok::MinusEq ||
             k == Tok::StarEq || k == Tok::SlashEq;
    };
    auto assignText = [](Tok k) -> std::string {
      if (k == Tok::PlusEq)
        return "+=";
      if (k == Tok::MinusEq)
        return "-=";
      if (k == Tok::StarEq)
        return "*=";
      if (k == Tok::SlashEq)
        return "/=";
      return "=";
    };
    auto matchAssignOp = [&](std::string &op) {
      if (!isAssignTok(peek().kind))
        return false;
      op = assignText(peek().kind);
      advance();
      return true;
    };
    if (check(Tok::Identifier) && isAssignTok(tokens[i + 1].kind)) {
      const Token &name = advance();
      const Tok opTok = advance().kind;
      Stmt s;
      s.kind = Stmt::Kind::Assign;
      s.name = name.text;
      s.op = assignText(opTok);
      s.expr = parseExpr();
      s.line = name.line;
      s.col = name.col;
      expect(Tok::Semi, "expected ';'");
      return s;
    }
    Stmt s;
    s.kind = Stmt::Kind::Expr;
    s.expr = parseExpr();
    s.line = s.expr.line;
    s.col = s.expr.col;
    std::string op;
    if (s.expr.kind == Expr::Kind::Member && matchAssignOp(op)) {
      s.kind = Stmt::Kind::FieldAssign;
      s.name = s.expr.text;
      s.op = std::move(op);
      s.target = std::move(s.expr.kids[0]);
      s.expr = parseExpr();
    } else if (s.expr.kind == Expr::Kind::Index && matchAssignOp(op)) {
      s.kind = Stmt::Kind::IndexAssign;
      s.op = std::move(op);
      s.target = std::move(s.expr);
      s.expr = parseExpr();
    }
    expect(Tok::Semi, "expected ';'");
    return s;
  }

  MatchArm parseMatchArm() {
    MatchArm arm;
    if (check(Tok::Integer)) {
      arm.pat = MatchArm::Pat::Int;
      arm.number = advance().number;
    } else if (check(Tok::Float)) {
      arm.pat = MatchArm::Pat::Float;
      arm.real = advance().real;
    } else if (check(Tok::String)) {
      arm.pat = MatchArm::Pat::String;
      arm.text = advance().text;
    } else if (match(Tok::True) || match(Tok::False)) {
      arm.pat = MatchArm::Pat::Bool;
      arm.boolean = (prev().kind == Tok::True);
    } else {
      const Token &name = expect(Tok::Identifier, "expected match pattern");
      if (name.text == "_") {
        arm.pat = MatchArm::Pat::Wildcard;
      } else {
        arm.pat = MatchArm::Pat::Variant;
        arm.name = name.text;
        while (match(Tok::Dot))
          arm.name = expect(Tok::Identifier, "expected name after '.'").text;
        if (match(Tok::LParen)) {
          if (!check(Tok::RParen)) {
            while (true) {
              if (check(Tok::Identifier) && tokens[i + 1].kind == Tok::Colon) {
                arm.fieldNames.push_back(advance().text);
                expect(Tok::Colon, "expected ':' after field name");
                arm.binds.push_back(
                    expect(Tok::Identifier, "expected binding name").text);
              } else {
                arm.fieldNames.push_back("");
                arm.binds.push_back(
                    expect(Tok::Identifier, "expected binding name").text);
              }
              if (!match(Tok::Comma))
                break;
              if (check(Tok::RParen))
                break;
            }
          }
          expect(Tok::RParen, "expected ')' after pattern binding");
        }
      }
    }
    arm.body = parseBlock();
    return arm;
  }

  Stmt parseMatchStmt() {
    const Token &kw = advance();
    Stmt s;
    s.kind = Stmt::Kind::Match;
    s.line = kw.line;
    s.col = kw.col;
    s.expr = parseExpr();
    expect(Tok::LBrace, "expected '{' before match arms");
    while (!check(Tok::RBrace) && !check(Tok::Eof))
      s.arms.push_back(parseMatchArm());
    expect(Tok::RBrace, "expected '}' after match arms");
    match(Tok::Semi);
    return s;
  }

  EnumDecl parseEnum() {
    const Token &enumTok = expect(Tok::Enum, "expected 'enum'");
    const Token &name = expect(Tok::Identifier, "expected enum name");
    EnumDecl e;
    e.name = name.text;
    e.line = enumTok.line;
    if (match(Tok::LArrow)) {
      int depth = 1;
      while (depth > 0 && !check(Tok::Eof)) {
        if (match(Tok::LArrow))
          ++depth;
        else if (match(Tok::RArrow))
          --depth;
        else
          advance();
      }
    }
    expect(Tok::LBrace, "expected '{'");
    while (!check(Tok::RBrace) && !check(Tok::Eof)) {
      match(Tok::Pub);
      const Token &vname = expect(Tok::Identifier, "expected variant name");
      EnumVariant v;
      v.name = vname.text;
      if (match(Tok::LParen)) {
        if (!check(Tok::RParen)) {
          while (true) {
            if (check(Tok::Identifier) && tokens[i + 1].kind == Tok::Colon) {
              v.fieldNames.push_back(advance().text);
              expect(Tok::Colon, "expected ':' after field name");
              parseType();
            } else {
              v.fieldNames.push_back("");
              parseType();
            }
            ++v.arity;
            if (!match(Tok::Comma))
              break;
            if (check(Tok::RParen))
              break;
          }
        }
        expect(Tok::RParen, "expected ')' after variant types");
      }
      for (const auto &prev : e.variants) {
        if (prev.name == v.name)
          errorNote("duplicate variant '" + v.name + "'");
      }
      e.variants.push_back(std::move(v));
      if (!match(Tok::Comma))
        break;
    }
    expect(Tok::RBrace, "expected '}' after enum variants");
    return e;
  }

  FnDecl parseFn(const FnAttrs &attrs, bool isPub, bool abstractMethod = false) {
    const Token &fnTok = expect(Tok::Function, "expected 'fn'");
    const Token &name = expect(Tok::Identifier, "expected function name");
    expect(Tok::LParen, "expected '('");
    FnDecl fn;
    fn.name = name.text;
    fn.isTest = attrs.isTest;
    fn.isDeprecated = attrs.isDeprecated;
    fn.isConstexpr = attrs.isConstexpr;
    fn.isUfcs = attrs.isUfcs;
    fn.isPub = isPub;
    fn.isAbstract = abstractMethod;
    fn.line = fnTok.line;
    if (!check(Tok::RParen)) {
      do {
        const Token &param = expect(Tok::Identifier, "expected parameter name");
        fn.paramTypes.push_back(parseOptionalType());
        fn.params.push_back(param.text);
      } while (match(Tok::Comma));
    }
    expect(Tok::RParen, "expected ')'");
    fn.throws = match(Tok::Throws);
    fn.returnType = parseOptionalType();
    if (abstractMethod) {
      expect(Tok::Semi, "expected ';' after abstract method");
      return fn;
    }
    fn.body = parseBlock();
    return fn;
  }

  void bindSelf(FnDecl &fn) {
    if (fn.params.empty() || fn.params[0] != "self") {
      fn.params.insert(fn.params.begin(), "self");
      fn.paramTypes.insert(fn.paramTypes.begin(), "");
    }
  }

  void checkMethodName(const std::vector<std::string> &fields,
                       const std::vector<FnDecl> &methods,
                       const std::string &name) {
    for (const auto &f : fields) {
      if (f == name)
        errorNote("method '" + name + "' conflicts with field '" + name + "'");
    }
    for (const auto &m : methods) {
      if (m.name == name)
        errorNote("duplicate method '" + name + "'");
    }
  }

  bool parseOneAttr(FnAttrs &attrs) {
    if (!match(Tok::At))
      return false;
    const Token &attr = expect(Tok::Identifier, "expected attribute name");
    if (attr.text == "test")
      attrs.isTest = true;
    else if (attr.text == "deprecated")
      attrs.isDeprecated = true;
    else if (attr.text == "constexpr")
      attrs.isConstexpr = true;
    else if (attr.text == "ufcs")
      attrs.isUfcs = true;
    else if (attr.text == "optional")
      attrs.isOptional = true;
    else
      errorNote("unknown attribute @" + attr.text);
    return true;
  }

  void rejectMethodAttrs(const FnAttrs &attrs) {
    if (attrs.isTest)
      errorNote("@test cannot apply to method");
    if (attrs.isUfcs)
      errorNote("@ufcs cannot apply to method");
    if (attrs.isOptional)
      errorNote("@optional cannot apply to method");
  }

  void rejectClassMods(bool isAbstract, bool isFinal, const char *what) {
    if (isAbstract)
      errorNote(std::string("abstract cannot apply to ") + what);
    if (isFinal)
      errorNote(std::string("final cannot apply to ") + what);
  }

  void rejectProtected(bool isProtected, const char *what) {
    if (isProtected)
      errorNote(std::string("protected cannot apply to ") + what);
  }

  struct MemberPrefix {
    FnAttrs attrs;
    bool isAbstract = false;
    bool isFinal = false;
    Vis vis = Vis::Pub;
    bool sawVis = false;
  };

  void takeVis(MemberPrefix &p, Vis v) {
    if (p.sawVis)
      errorNote("cannot combine visibility modifiers");
    p.sawVis = true;
    p.vis = v;
  }

  void parseMemberPrefix(MemberPrefix &p) {
    while (true) {
      if (parseOneAttr(p.attrs))
        continue;
      if (match(Tok::Pub)) {
        takeVis(p, Vis::Pub);
        continue;
      }
      if (match(Tok::Private)) {
        takeVis(p, Vis::Private);
        continue;
      }
      if (match(Tok::Protected)) {
        takeVis(p, Vis::Protected);
        continue;
      }
      if (match(Tok::Abstract)) {
        p.isAbstract = true;
        continue;
      }
      if (match(Tok::Final)) {
        p.isFinal = true;
        continue;
      }
      break;
    }
  }

  FnDecl parseMethod(bool traitImpl = false) {
    MemberPrefix p;
    parseMemberPrefix(p);
    rejectClassMods(p.isAbstract, p.isFinal, "impl method");
    if (traitImpl && p.vis != Vis::Pub)
      errorNote("trait impl methods cannot be private or protected");
    rejectMethodAttrs(p.attrs);
    FnDecl fn = parseFn(p.attrs, true);
    fn.vis = p.vis;
    bindSelf(fn);
    return fn;
  }

  structDecl parseStruct(bool isData) {
    const Token &tok =
        isData ? expect(Tok::Data, "expected 'data'")
               : expect(Tok::Struct, "expected 'struct'");
    const Token &name = expect(Tok::Identifier, isData ? "expected data name"
                                                       : "expected struct name");
    structDecl s;
    s.name = name.text;
    s.line = tok.line;
    s.isData = isData;
    const char *kind = isData ? "data" : "struct";
    if (match(Tok::Implements)) {
      do {
        s.implTraits.push_back(
            expect(Tok::Identifier, "expected trait name after impl").text);
      } while (match(Tok::Comma));
    }
    expect(Tok::LBrace, "expected '{'");
    while (!check(Tok::RBrace) && !check(Tok::Eof)) {
      try {
        MemberPrefix p;
        parseMemberPrefix(p);
        if (check(Tok::Function)) {
          rejectClassMods(p.isAbstract, p.isFinal,
                          (std::string(kind) + " method").c_str());
          if (p.vis == Vis::Protected)
            errorNote(std::string("protected cannot apply to ") + kind +
                      " method");
          rejectMethodAttrs(p.attrs);
          FnDecl fn = parseFn(p.attrs, true);
          fn.vis = p.vis;
          bindSelf(fn);
          checkMethodName(s.fields, s.methods, fn.name);
          s.methods.push_back(std::move(fn));
          continue;
        }
        rejectClassMods(p.isAbstract, p.isFinal, "field");
        if (p.vis == Vis::Protected)
          errorNote(std::string("protected cannot apply to ") + kind +
                    " field");
        if (p.attrs.fnLike())
          errorNote("attributes cannot apply to field");
        const Token &field = expect(Tok::Identifier, "expected field name");
        std::string ty = parseOptionalType();
        expect(Tok::Semi, "expected ';'");
        for (const auto &f : s.fields) {
          if (f == field.text)
            errorNote("duplicate field '" + field.text + "'");
        }
        for (const auto &m : s.methods) {
          if (m.name == field.text)
            errorNote("field '" + field.text + "' conflicts with method '" +
                      field.text + "'");
        }
        s.fields.push_back(field.text);
        s.fieldTypes.push_back(std::move(ty));
        s.fieldOptional.push_back(p.attrs.isOptional ? 1 : 0);
        s.fieldVis.push_back(static_cast<char>(p.vis));
      } catch (const ParseError &) {
        synchronizeStmt();
      }
    }
    expect(Tok::RBrace, "expected '}'");
    return s;
  }

  ImplDecl parseImpl() {
    const Token &implTok = expect(Tok::Implements, "expected 'impl'");
    const Token &first = expect(Tok::Identifier, "expected type or trait name");
    ImplDecl impl;
    impl.line = implTok.line;
    if (match(Tok::For)) {
      impl.traitName = first.text;
      impl.typeName =
          expect(Tok::Identifier, "expected type name after 'impl Trait for'")
              .text;
    } else {
      impl.typeName = first.text;
    }
    expect(Tok::LBrace, "expected '{'");
    while (!check(Tok::RBrace) && !check(Tok::Eof)) {
      try {
        FnDecl fn = parseMethod(!impl.traitName.empty());
        checkMethodName({}, impl.methods, fn.name);
        impl.methods.push_back(std::move(fn));
      } catch (const ParseError &) {
        synchronizeStmt();
      }
    }
    expect(Tok::RBrace, "expected '}'");
    return impl;
  }

  TraitMethod parseTraitMethod() {
    const Token &fnTok = expect(Tok::Function, "expected 'fn'");
    const Token &name = expect(Tok::Identifier, "expected function name");
    expect(Tok::LParen, "expected '('");
    TraitMethod m;
    m.name = name.text;
    m.line = fnTok.line;
    if (!check(Tok::RParen)) {
      do {
        const Token &param = expect(Tok::Identifier, "expected parameter name");
        m.paramTypes.push_back(parseOptionalType());
        m.params.push_back(param.text);
      } while (match(Tok::Comma));
    }
    expect(Tok::RParen, "expected ')'");
    m.throws = match(Tok::Throws);
    m.returnType = parseOptionalType();
    expect(Tok::Semi, "expected ';' after trait method (signatures only)");
    if (m.params.empty() || m.params[0] != "self") {
      m.params.insert(m.params.begin(), "self");
      m.paramTypes.insert(m.paramTypes.begin(), "");
    }
    return m;
  }

  TraitDecl parseTrait() {
    const Token &traitTok = expect(Tok::Trait, "expected 'trait'");
    const Token &name = expect(Tok::Identifier, "expected trait name");
    TraitDecl t;
    t.name = name.text;
    t.line = traitTok.line;
    expect(Tok::LBrace, "expected '{'");
    while (!check(Tok::RBrace) && !check(Tok::Eof)) {
      try {
        match(Tok::Pub);
        if (check(Tok::Signal)) {
          t.signals.push_back(parseSignal());
          continue;
        }
        if (check(Tok::Function)) {
          TraitMethod m = parseTraitMethod();
          for (const auto &prev : t.methods) {
            if (prev.name == m.name)
              errorNote("duplicate method '" + m.name + "'");
          }
          t.methods.push_back(std::move(m));
          continue;
        }
        if (check(Tok::Variable) || check(Tok::Constant)) {
          errorNote("traits cannot declare vars or consts");
          synchronizeStmt();
          continue;
        }
        errorHere("expected fn signature or signal in trait");
      } catch (const ParseError &) {
        synchronizeStmt();
      }
    }
    expect(Tok::RBrace, "expected '}'");
    return t;
  }

  ClassDecl parseClass() {
    const Token &classTok = expect(Tok::Class, "expected 'class'");
    const Token &name = expect(Tok::Identifier, "expected class name");
    ClassDecl c;
    c.name = name.text;
    c.line = classTok.line;
    if (match(Tok::Extends))
      c.parent =
          expect(Tok::Identifier, "expected parent class name after extends")
              .text;
    if (match(Tok::Implements)) {
      do {
        c.implTraits.push_back(
            expect(Tok::Identifier, "expected trait name after impl").text);
      } while (match(Tok::Comma));
    }
    expect(Tok::LBrace, "expected '{'");
    std::vector<std::string> fieldNames;
    while (!check(Tok::RBrace) && !check(Tok::Eof)) {
      try {
      MemberPrefix p;
      parseMemberPrefix(p);
      if (check(Tok::Constant)) {
        errorNote("class const is not v1 (use a module-level const)");
        synchronizeStmt();
        continue;
      }
      if (check(Tok::Implements)) {
        if (p.attrs.any())
          errorNote("attributes cannot apply to impl");
        rejectClassMods(p.isAbstract, p.isFinal, "impl");
        if (p.sawVis)
          errorNote("visibility cannot apply to impl");
        expect(Tok::Implements, "expected 'impl'");
        const Token &trait =
            expect(Tok::Identifier, "expected trait name after impl");
        if (match(Tok::For))
          errorNote("impl inside a class is `impl Trait { … }` (no `for`)");
        NestedImpl block;
        block.traitName = trait.text;
        expect(Tok::LBrace, "expected '{' after trait name");
        while (!check(Tok::RBrace) && !check(Tok::Eof)) {
          FnDecl fn = parseMethod(true);
          checkMethodName(fieldNames, c.methods, fn.name);
          for (const auto &b : c.traitImpls) {
            checkMethodName({}, b.methods, fn.name);
          }
          block.methods.push_back(std::move(fn));
        }
        expect(Tok::RBrace, "expected '}'");
        c.traitImpls.push_back(std::move(block));
        continue;
      }
      if (check(Tok::Function)) {
        rejectMethodAttrs(p.attrs);
        if (p.isAbstract && p.isFinal)
          errorNote("method cannot be both abstract and final");
        if (p.isAbstract && p.attrs.isConstexpr)
          errorNote("@constexpr cannot apply to abstract method");
        FnDecl fn = parseFn(p.attrs, true, p.isAbstract);
        fn.isFinal = p.isFinal;
        fn.vis = p.vis;
        bindSelf(fn);
        checkMethodName(fieldNames, c.methods, fn.name);
        for (const auto &b : c.traitImpls) {
          checkMethodName({}, b.methods, fn.name);
        }
        c.methods.push_back(std::move(fn));
        continue;
      }
      if (!check(Tok::Variable))
        errorHere("expected var, fn, or impl in class body");
      rejectClassMods(p.isAbstract, p.isFinal, "field");
      if (p.attrs.fnLike())
        errorNote("attributes cannot apply to field");
      expect(Tok::Variable, "expected 'var'");
      const Token &field = expect(Tok::Identifier, "expected field name");
      ClassField f;
      f.name = field.text;
      f.type = parseOptionalType();
      f.optional = p.attrs.isOptional;
      f.vis = p.vis;
      if (match(Tok::Eq)) {
        f.hasDefault = true;
        f.defaultValue = parseExpr();
      }
      expect(Tok::Semi, "expected ';'");
      for (const auto &prev : fieldNames) {
        if (prev == f.name)
          errorNote("duplicate field '" + f.name + "'");
      }
      checkMethodName(fieldNames, c.methods, f.name);
      for (const auto &b : c.traitImpls) {
        for (const auto &m : b.methods) {
          if (m.name == f.name)
            errorNote("field '" + f.name + "' conflicts with method '" +
                      f.name + "'");
        }
      }
      fieldNames.push_back(f.name);
      c.fields.push_back(std::move(f));
      } catch (const ParseError &) {
        synchronizeStmt();
      }
    }
    expect(Tok::RBrace, "expected '}'");
    c.shape.name = c.name;
    c.shape.line = c.line;
    for (const auto &f : c.fields) {
      c.shape.fields.push_back(f.name);
      c.shape.fieldTypes.push_back(f.type);
      c.shape.fieldOptional.push_back(f.optional ? 1 : 0);
    }
    return c;
  }

  SignalDecl parseSignal() {
    const Token &sigTok = expect(Tok::Signal, "expected 'signal'");
    const Token &name = expect(Tok::Identifier, "expected signal name");
    expect(Tok::LParen, "expected '('");
    SignalDecl sig;
    sig.name = name.text;
    sig.line = sigTok.line;
    if (!check(Tok::RParen)) {
      do {
        const Token &param = expect(Tok::Identifier, "expected parameter name");
        parseOptionalType();
        sig.params.push_back(param.text);
      } while (match(Tok::Comma));
    }
    expect(Tok::RParen, "expected ')'");
    expect(Tok::Semi, "expected ';'");
    return sig;
  }

  ImportDecl parseImport() {
    ImportDecl d;
    d.line = peek().line;
    d.col = peek().col;
    if (match(Tok::From)) {
      d.isFrom = true;
      const Token &mod = expect(Tok::Identifier, "expected module name");
      expect(Tok::Import, "expected 'import' after module name");
      const Token &item = expect(Tok::Identifier, "expected imported name");
      d.path.push_back(mod.text);
      d.path.push_back(item.text);
      while (match(Tok::Dot))
        d.path.push_back(
            expect(Tok::Identifier, "expected name after '.'").text);
      if (match(Tok::As))
        d.alias = expect(Tok::Identifier, "expected alias").text;
      expect(Tok::Semi, "expected ';' after import");
      return d;
    }
    expect(Tok::Import, "expected 'import'");
    d.path.push_back(
        expect(Tok::Identifier, "expected module name").text);
    while (match(Tok::Dot))
      d.path.push_back(
          expect(Tok::Identifier, "expected module name after '.'").text);
    if (match(Tok::As))
      d.alias = expect(Tok::Identifier, "expected alias").text;
    expect(Tok::Semi, "expected ';' after import");
    return d;
  }

  void parseItem(Program *prog, ModDecl *mod) {
    const bool inMod = mod != nullptr;
    bool isPub = !inMod;
    bool isAbstract = false;
    bool isFinal = false;
    bool isProtected = false;
    bool sawVis = false;
    FnAttrs attrs;
    auto takeItemVis = [&](bool pub) {
      if (sawVis)
        errorNote("cannot combine visibility modifiers");
      sawVis = true;
      isPub = pub;
      isProtected = false;
    };
    while (true) {
      if (parseOneAttr(attrs))
        continue;
      if (match(Tok::Pub)) {
        takeItemVis(true);
        continue;
      }
      if (match(Tok::Private)) {
        takeItemVis(false);
        continue;
      }
      if (match(Tok::Protected)) {
        if (sawVis)
          errorNote("cannot combine visibility modifiers");
        sawVis = true;
        isProtected = true;
        continue;
      }
      if (match(Tok::Abstract)) {
        isAbstract = true;
        continue;
      }
      if (match(Tok::Final)) {
        isFinal = true;
        continue;
      }
      break;
    }
    if (check(Tok::Import) || check(Tok::From)) {
      if (attrs.any())
        errorNote("attributes cannot apply to import");
      rejectClassMods(isAbstract, isFinal, "import");
      rejectProtected(isProtected, "import");
      ImportDecl im = parseImport();
      if (inMod)
        mod->imports.push_back(std::move(im));
      else
        prog->imports.push_back(std::move(im));
      return;
    }
    if (check(Tok::Module)) {
      if (attrs.any())
        errorNote("attributes cannot apply to mod");
      rejectClassMods(isAbstract, isFinal, "mod");
      rejectProtected(isProtected, "mod");
      ModDecl nested = parseMod();
      nested.isPub = isPub;
      if (inMod)
        mod->mods.push_back(std::move(nested));
      else
        prog->mods.push_back(std::move(nested));
      return;
    }
    if (check(Tok::Struct) || check(Tok::Data)) {
      const bool isData = check(Tok::Data);
      const char *kind = isData ? "data" : "struct";
      if (attrs.isTest)
        errorNote(std::string("@test cannot apply to ") + kind);
      if (attrs.isConstexpr)
        errorNote(std::string("@constexpr cannot apply to ") + kind);
      if (attrs.isUfcs)
        errorNote(std::string("@ufcs cannot apply to ") + kind);
      if (attrs.isOptional)
        errorNote(std::string("@optional cannot apply to ") + kind);
      rejectClassMods(isAbstract, isFinal, kind);
      rejectProtected(isProtected, kind);
      structDecl s = parseStruct(isData);
      s.isPub = isPub;
      if (inMod)
        mod->structs.push_back(std::move(s));
      else
        prog->structs.push_back(std::move(s));
      return;
    }
    if (check(Tok::Class)) {
      if (attrs.isTest)
        errorNote("@test cannot apply to class");
      if (attrs.isConstexpr)
        errorNote("@constexpr cannot apply to class");
      if (attrs.isUfcs)
        errorNote("@ufcs cannot apply to class");
      if (attrs.isOptional)
        errorNote("@optional cannot apply to class");
      if (isAbstract && isFinal)
        errorNote("class cannot be both abstract and final");
      rejectProtected(isProtected, "class");
      ClassDecl c = parseClass();
      c.isPub = isPub;
      c.isAbstract = isAbstract;
      c.isFinal = isFinal;
      c.shape.isPub = isPub;
      if (inMod)
        mod->classes.push_back(std::move(c));
      else
        prog->classes.push_back(std::move(c));
      return;
    }
    if (check(Tok::Trait)) {
      if (attrs.isTest)
        errorNote("@test cannot apply to trait");
      if (attrs.isConstexpr)
        errorNote("@constexpr cannot apply to trait");
      if (attrs.isUfcs)
        errorNote("@ufcs cannot apply to trait");
      if (attrs.isOptional)
        errorNote("@optional cannot apply to trait");
      rejectClassMods(isAbstract, isFinal, "trait");
      rejectProtected(isProtected, "trait");
      TraitDecl t = parseTrait();
      t.isPub = isPub;
      if (inMod)
        mod->traits.push_back(std::move(t));
      else
        prog->traits.push_back(std::move(t));
      return;
    }
    if (check(Tok::Enum)) {
      if (attrs.isTest)
        errorNote("@test cannot apply to enum");
      if (attrs.isConstexpr)
        errorNote("@constexpr cannot apply to enum");
      if (attrs.isUfcs)
        errorNote("@ufcs cannot apply to enum");
      if (attrs.isOptional)
        errorNote("@optional cannot apply to enum");
      rejectClassMods(isAbstract, isFinal, "enum");
      rejectProtected(isProtected, "enum");
      EnumDecl e = parseEnum();
      e.isPub = isPub;
      if (inMod)
        mod->enums.push_back(std::move(e));
      else
        prog->enums.push_back(std::move(e));
      return;
    }
    if (check(Tok::Implements)) {
      if (attrs.any())
        errorNote("attributes cannot apply to impl");
      rejectClassMods(isAbstract, isFinal, "impl");
      rejectProtected(isProtected, "impl");
      ImplDecl impl = parseImpl();
      if (inMod)
        mod->impls.push_back(std::move(impl));
      else
        prog->impls.push_back(std::move(impl));
      return;
    }
    if (check(Tok::Signal)) {
      if (attrs.any())
        errorNote("attributes cannot apply to signal");
      rejectClassMods(isAbstract, isFinal, "signal");
      rejectProtected(isProtected, "signal");
      SignalDecl sig = parseSignal();
      sig.isPub = isPub;
      if (inMod)
        mod->signals.push_back(std::move(sig));
      else
        prog->signals.push_back(std::move(sig));
      return;
    }
    rejectClassMods(isAbstract, isFinal, "function");
    rejectProtected(isProtected, "function");
    FnDecl fn = parseFn(attrs, isPub);
    if (attrs.isOptional)
      errorNote("@optional cannot apply to function");
    if (inMod)
      mod->fns.push_back(std::move(fn));
    else
      prog->fns.push_back(std::move(fn));
  }

  ModDecl parseMod() {
    const Token &modTok = expect(Tok::Module, "expected 'mod'");
    const Token &name = expect(Tok::Identifier, "expected module name");
    ModDecl m;
    m.name = name.text;
    m.line = modTok.line;
    expect(Tok::LBrace, "expected '{' after module name");
    while (!check(Tok::RBrace) && !check(Tok::Eof)) {
      try {
        parseItem(nullptr, &m);
      } catch (const ParseError &) {
        synchronizeItem();
      }
    }
    expect(Tok::RBrace, "expected '}' after module body");
    return m;
  }

  Program parse() {
    Program program;
    while (!check(Tok::Eof)) {
      try {
        parseItem(&program, nullptr);
      } catch (const ParseError &) {
        synchronizeItem();
      }
    }
    return program;
  }
};

} // namespace

Program parseSource(const std::string &source, const std::string &file,
                    std::vector<Diagnostic> *errors) {
  std::vector<Diagnostic> local;
  std::vector<Diagnostic> *out = errors ? errors : &local;
  Program program = Parser(tokenize(source, file, out), file, out).parse();
  if (!errors && !local.empty())
    throw std::runtime_error(diagnosticError(local[0], file));
  return program;
}
