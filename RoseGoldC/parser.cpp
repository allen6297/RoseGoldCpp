#include "parser.h"
#include "ast.h"
#include "lexer.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Parser {
  std::vector<Token> tokens;
  std::string file;
  size_t i = 0;

  explicit Parser(std::vector<Token> t, std::string f)
      : tokens(std::move(t)), file(std::move(f)) {}

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

  [[noreturn]] void errorHere(const std::string &msg) const {
    throw std::runtime_error(
        locatedError("parse error", file, peek().line, peek().col, msg));
  }

  const Token &expect(Tok k, const std::string &msg) {
    if (!check(k))
      errorHere(msg);
    return advance();
  }

  void parseType() {
    expect(Tok::Identifier, "expected type name");
    if (!match(Tok::LArrow))
      return;
    if (!check(Tok::RArrow)) {
      do {
        parseType();
      } while (match(Tok::Comma));
    }
    expect(Tok::RArrow, "expected '>'");
  }

  void skipTypeAnnotation() {
    if (match(Tok::Colon))
      parseType();
  }

  Expr make(Expr::Kind kind, int line, int col) {
    Expr e;
    e.kind = kind;
    e.line = line;
    e.col = col;
    return e;
  }

  Expr parsePrimary() {
    const Token &t = peek();
    if (match(Tok::Integer)) {
      Expr e = make(Expr::Kind::Int, t.line, t.col);
      e.number = t.number;
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
    while (match(Tok::LArrow) || match(Tok::RArrow)) {
      const Token &op = prev();
      Expr e = make(Expr::Kind::Binary, op.line, op.col);
      e.text = op.text;
      e.kids.push_back(std::move(left));
      e.kids.push_back(parseTerm());
      left = std::move(e);
    }
    return left;
  }

  Expr parseExpr() {
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

  std::vector<Stmt> parseBlock() {
    expect(Tok::LBrace, "expected '{'");
    std::vector<Stmt> stmts;
    while (!check(Tok::RBrace) && !check(Tok::Eof))
      stmts.push_back(parseStmt());
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
    if (match(Tok::While)) {
      Stmt s;
      s.kind = Stmt::Kind::While;
      s.line = t.line;
      s.col = t.col;
      s.expr = parseExpr();
      s.body = parseBlock();
      return s;
    }
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
      skipTypeAnnotation();
      expect(Tok::Eq, "expected '='");
      Stmt s;
      s.kind = isConst ? Stmt::Kind::Const : Stmt::Kind::Var;
      s.name = name.text;
      s.expr = parseExpr();
      s.line = name.line;
      s.col = name.col;
      expect(Tok::Semi, "expected ';'");
      return s;
    }
    if (check(Tok::Identifier) && tokens[i + 1].kind == Tok::Eq) {
      const Token &name = advance();
      advance();
      Stmt s;
      s.kind = Stmt::Kind::Assign;
      s.name = name.text;
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
    if (s.expr.kind == Expr::Kind::Member && match(Tok::Eq)) {
      s.kind = Stmt::Kind::FieldAssign;
      s.name = s.expr.text;
      s.target = std::move(s.expr.kids[0]);
      s.expr = parseExpr();
    }
    expect(Tok::Semi, "expected ';'");
    return s;
  }
  // MARK: DECLARATIONS
  FnDecl parseFn(bool isTest, bool isDeprecated, bool isPub) {
    const Token &fnTok = expect(Tok::Function, "expected 'fn'");
    const Token &name = expect(Tok::Identifier, "expected function name");
    expect(Tok::LParen, "expected '('");
    FnDecl fn;
    fn.name = name.text;
    fn.isTest = isTest;
    fn.isDeprecated = isDeprecated;
    fn.isPub = isPub;
    fn.line = fnTok.line;
    if (!check(Tok::RParen)) {
      do {
        const Token &param = expect(Tok::Identifier, "expected parameter name");
        skipTypeAnnotation();
        fn.params.push_back(param.text);
      } while (match(Tok::Comma));
    }
    expect(Tok::RParen, "expected ')'");
    skipTypeAnnotation();
    fn.body = parseBlock();
    return fn;
  }

  void bindSelf(FnDecl &fn) {
    if (fn.params.empty() || fn.params[0] != "self")
      fn.params.insert(fn.params.begin(), "self");
  }

  void checkMethodName(const std::vector<std::string> &fields,
                       const std::vector<FnDecl> &methods,
                       const std::string &name) {
    for (const auto &f : fields) {
      if (f == name)
        errorHere("method '" + name + "' conflicts with field '" + name + "'");
    }
    for (const auto &m : methods) {
      if (m.name == name)
        errorHere("duplicate method '" + name + "'");
    }
  }

  bool parseOneAttr(bool &isTest, bool &isDeprecated) {
    if (!match(Tok::At))
      return false;
    const Token &attr = expect(Tok::Identifier, "expected attribute name");
    if (attr.text == "test")
      isTest = true;
    else if (attr.text == "deprecated")
      isDeprecated = true;
    else
      errorHere("unknown attribute @" + attr.text);
    return true;
  }

  FnDecl parseMethod() {
    bool isTest = false;
    bool isDeprecated = false;
    parseOneAttr(isTest, isDeprecated);
    if (isTest)
      errorHere("@test cannot apply to method");
    FnDecl fn = parseFn(false, isDeprecated, true);
    bindSelf(fn);
    return fn;
  }

  structDecl parseStruct() {
    const Token &structTok = expect(Tok::Struct, "expected 'struct'");
    const Token &name = expect(Tok::Identifier, "expected struct name");
    structDecl s;
    s.name = name.text;
    s.line = structTok.line;
    expect(Tok::LBrace, "expected '{'");
    while (!check(Tok::RBrace) && !check(Tok::Eof)) {
      bool isTest = false;
      bool isDeprecated = false;
      parseOneAttr(isTest, isDeprecated);
      if (check(Tok::Function)) {
        if (isTest)
          errorHere("@test cannot apply to method");
        FnDecl fn = parseFn(false, isDeprecated, true);
        bindSelf(fn);
        checkMethodName(s.fields, s.methods, fn.name);
        s.methods.push_back(std::move(fn));
        continue;
      }
      if (isTest || isDeprecated)
        errorHere("attributes cannot apply to field");
      const Token &field = expect(Tok::Identifier, "expected field name");
      skipTypeAnnotation();
      expect(Tok::Semi, "expected ';'");
      for (const auto &f : s.fields) {
        if (f == field.text)
          errorHere("duplicate field '" + field.text + "'");
      }
      for (const auto &m : s.methods) {
        if (m.name == field.text)
          errorHere("field '" + field.text + "' conflicts with method '" +
                    field.text + "'");
      }
      s.fields.push_back(field.text);
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
      FnDecl fn = parseMethod();
      checkMethodName({}, impl.methods, fn.name);
      impl.methods.push_back(std::move(fn));
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
        skipTypeAnnotation();
        m.params.push_back(param.text);
      } while (match(Tok::Comma));
    }
    expect(Tok::RParen, "expected ')'");
    skipTypeAnnotation();
    expect(Tok::Semi, "expected ';' after trait method (signatures only)");
    if (m.params.empty() || m.params[0] != "self")
      m.params.insert(m.params.begin(), "self");
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
      match(Tok::Pub);
      if (check(Tok::Signal)) {
        t.signals.push_back(parseSignal());
        continue;
      }
      if (check(Tok::Function)) {
        TraitMethod m = parseTraitMethod();
        for (const auto &prev : t.methods) {
          if (prev.name == m.name)
            errorHere("duplicate method '" + m.name + "'");
        }
        t.methods.push_back(std::move(m));
        continue;
      }
      if (check(Tok::Variable) || check(Tok::Constant))
        errorHere("traits cannot declare vars or consts");
      errorHere("expected fn signature or signal in trait");
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
      bool isTest = false;
      bool isDeprecated = false;
      while (true) {
        if (parseOneAttr(isTest, isDeprecated))
          continue;
        if (match(Tok::Pub))
          continue;
        break;
      }
      if (check(Tok::Constant))
        errorHere("class const is not v1 (use a module-level const)");
      if (check(Tok::Implements)) {
        if (isTest || isDeprecated)
          errorHere("attributes cannot apply to impl");
        expect(Tok::Implements, "expected 'impl'");
        const Token &trait =
            expect(Tok::Identifier, "expected trait name after impl");
        if (match(Tok::For))
          errorHere("impl inside a class is `impl Trait { … }` (no `for`)");
        NestedImpl block;
        block.traitName = trait.text;
        expect(Tok::LBrace, "expected '{' after trait name");
        while (!check(Tok::RBrace) && !check(Tok::Eof)) {
          FnDecl fn = parseMethod();
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
        if (isTest)
          errorHere("@test cannot apply to method");
        FnDecl fn = parseFn(false, isDeprecated, true);
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
      if (isTest || isDeprecated)
        errorHere("attributes cannot apply to field");
      expect(Tok::Variable, "expected 'var'");
      const Token &field = expect(Tok::Identifier, "expected field name");
      skipTypeAnnotation();
      ClassField f;
      f.name = field.text;
      if (match(Tok::Eq)) {
        f.hasDefault = true;
        f.defaultValue = parseExpr();
      }
      expect(Tok::Semi, "expected ';'");
      for (const auto &prev : fieldNames) {
        if (prev == f.name)
          errorHere("duplicate field '" + f.name + "'");
      }
      checkMethodName(fieldNames, c.methods, f.name);
      for (const auto &b : c.traitImpls) {
        for (const auto &m : b.methods) {
          if (m.name == f.name)
            errorHere("field '" + f.name + "' conflicts with method '" +
                      f.name + "'");
        }
      }
      fieldNames.push_back(f.name);
      c.fields.push_back(std::move(f));
    }
    expect(Tok::RBrace, "expected '}'");
    c.shape.name = c.name;
    c.shape.line = c.line;
    for (const auto &f : c.fields)
      c.shape.fields.push_back(f.name);
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
        skipTypeAnnotation();
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
    bool isTest = false;
    bool isDeprecated = false;
    while (true) {
      if (parseOneAttr(isTest, isDeprecated))
        continue;
      if (match(Tok::Pub)) {
        isPub = true;
        continue;
      }
      break;
    }
    if (check(Tok::Import) || check(Tok::From)) {
      if (isTest || isDeprecated)
        errorHere("attributes cannot apply to import");
      ImportDecl im = parseImport();
      if (inMod)
        mod->imports.push_back(std::move(im));
      else
        prog->imports.push_back(std::move(im));
      return;
    }
    if (check(Tok::Module)) {
      if (isTest || isDeprecated)
        errorHere("attributes cannot apply to mod");
      ModDecl nested = parseMod();
      nested.isPub = isPub;
      if (inMod)
        mod->mods.push_back(std::move(nested));
      else
        prog->mods.push_back(std::move(nested));
      return;
    }
    if (check(Tok::Struct)) {
      if (isTest)
        errorHere("@test cannot apply to struct");
      structDecl s = parseStruct();
      s.isPub = isPub;
      if (inMod)
        mod->structs.push_back(std::move(s));
      else
        prog->structs.push_back(std::move(s));
      return;
    }
    if (check(Tok::Class)) {
      if (isTest)
        errorHere("@test cannot apply to class");
      ClassDecl c = parseClass();
      c.isPub = isPub;
      c.shape.isPub = isPub;
      if (inMod)
        mod->classes.push_back(std::move(c));
      else
        prog->classes.push_back(std::move(c));
      return;
    }
    if (check(Tok::Trait)) {
      if (isTest)
        errorHere("@test cannot apply to trait");
      TraitDecl t = parseTrait();
      t.isPub = isPub;
      if (inMod)
        mod->traits.push_back(std::move(t));
      else
        prog->traits.push_back(std::move(t));
      return;
    }
    if (check(Tok::Implements)) {
      if (isTest || isDeprecated)
        errorHere("attributes cannot apply to impl");
      ImplDecl impl = parseImpl();
      if (inMod)
        mod->impls.push_back(std::move(impl));
      else
        prog->impls.push_back(std::move(impl));
      return;
    }
    if (check(Tok::Signal)) {
      if (isTest || isDeprecated)
        errorHere("attributes cannot apply to signal");
      SignalDecl sig = parseSignal();
      sig.isPub = isPub;
      if (inMod)
        mod->signals.push_back(std::move(sig));
      else
        prog->signals.push_back(std::move(sig));
      return;
    }
    FnDecl fn = parseFn(isTest, isDeprecated, isPub);
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
    while (!check(Tok::RBrace) && !check(Tok::Eof))
      parseItem(nullptr, &m);
    expect(Tok::RBrace, "expected '}' after module body");
    return m;
  }

  Program parse() {
    Program program;
    while (!check(Tok::Eof))
      parseItem(&program, nullptr);
    return program;
  }
};

} // namespace

Program parseSource(const std::string &source, const std::string &file) {
  return Parser(tokenize(source, file), file).parse();
}
