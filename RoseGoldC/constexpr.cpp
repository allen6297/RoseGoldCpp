#include "interp.h"
#include "parser.h"
#include "lexer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

void Interpreter::checkConstexprCall(const std::string &name, int line, int col) {
  if (name == "len")
    return;
  if (name == "print" || name == "assert" || name == "argv" ||
      name == "argv_len")
    constexprFail(line, col,
                  "constexpr function cannot call '" + name + "'");
  FnDecl *fn = findLocalFn(name);
  if (!fn)
    constexprFail(line, col,
                  "constexpr function can only call constexpr functions ('" +
                      name + "' is unknown)");
  if (!fn->isConstexpr)
    constexprFail(line, col,
                  "constexpr function can only call constexpr functions ('" +
                      name + "' is not constexpr)");
}

void Interpreter::checkConstexprExpr(const Expr &e) {
  if (e.kind == Expr::Kind::Var && e.text == "super")
    constexprFail(e.line, e.col, "constexpr function cannot use super");
  if (e.kind == Expr::Kind::Call)
    checkConstexprCall(e.text, e.line, e.col);
  if (e.kind == Expr::Kind::MethodCall) {
    const Expr &recv = e.kids[0];
    bool allowed = false;
    if (recv.kind == Expr::Kind::Var) {
      if (signalArity.count(recv.text))
        constexprFail(e.line, e.col, "constexpr function cannot use signals");
      if (const std::string *modName = findModuleBind(recv.text)) {
        auto lit = loaded.find(*modName);
        if (lit == loaded.end())
          constexprFail(e.line, e.col, "unknown module '" + *modName + "'");
        auto eit = lit->second.exports.find(e.text);
        if (eit == lit->second.exports.end() || !eit->second->isConstexpr)
          constexprFail(e.line, e.col,
                        "constexpr function can only call constexpr "
                        "functions ('" +
                            recv.text + "." + e.text +
                            "' is not constexpr)");
        allowed = true;
      } else if (findEnum(recv.text))
        allowed = true;
    }
    if (!allowed && e.text != "len" && e.text != "has" && e.text != "keys")
      constexprFail(e.line, e.col,
                    "constexpr function cannot call method '" + e.text +
                        "'");
  }
  for (const auto &kid : e.kids)
    checkConstexprExpr(kid);
}

void Interpreter::checkConstexprStmt(const Stmt &stmt) {
  switch (stmt.kind) {
  case Stmt::Kind::Var:
    constexprFail(stmt.line, stmt.col, "constexpr function cannot use 'var'");
  case Stmt::Kind::Assign:
  case Stmt::Kind::FieldAssign:
  case Stmt::Kind::IndexAssign:
    constexprFail(stmt.line, stmt.col, "constexpr function cannot assign");
  case Stmt::Kind::While:
    constexprFail(stmt.line, stmt.col,
                  "constexpr function cannot use 'while'");
  case Stmt::Kind::For:
    constexprFail(stmt.line, stmt.col, "constexpr function cannot use 'for'");
  case Stmt::Kind::Break:
    constexprFail(stmt.line, stmt.col,
                  "constexpr function cannot use 'break'");
  case Stmt::Kind::Continue:
    constexprFail(stmt.line, stmt.col,
                  "constexpr function cannot use 'continue'");
  case Stmt::Kind::Pass:
    return;
  case Stmt::Kind::Expr:
  case Stmt::Kind::Const:
  case Stmt::Kind::Return:
    checkConstexprExpr(stmt.expr);
    return;
  case Stmt::Kind::If:
    checkConstexprExpr(stmt.expr);
    for (const auto &s : stmt.body)
      checkConstexprStmt(s);
    for (const auto &s : stmt.elseBody)
      checkConstexprStmt(s);
    return;
  case Stmt::Kind::Match:
    checkConstexprExpr(stmt.expr);
    for (const auto &arm : stmt.arms) {
      for (const auto &s : arm.body)
        checkConstexprStmt(s);
    }
    return;
  }
}

void Interpreter::checkConstexprFn(const FnDecl &fn) {
  for (const auto &stmt : fn.body)
    checkConstexprStmt(stmt);
}

void Interpreter::checkConstexprFns() {
  for (const auto &fn : program.fns) {
    if (fn.isConstexpr)
      checkConstexprFn(fn);
  }
  for (const auto &type : typeMethods) {
    for (const auto &m : type.second) {
      if (m.second->isConstexpr)
        checkConstexprFn(*m.second);
    }
  }
  const std::string prev = currentModule;
  for (auto &mod : loaded) {
    currentModule = mod.first;
    for (const auto &kv : mod.second.fns) {
      if (kv.second->isConstexpr)
        checkConstexprFn(*kv.second);
    }
  }
  currentModule = prev;
}
