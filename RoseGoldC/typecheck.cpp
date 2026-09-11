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

struct TypeChecker {
  Interpreter *interp;
  std::vector<std::map<std::string, std::string>> scopes;
  std::string currentReturn;
  std::string currentSelf;
  std::string currentSuper;
  bool currentThrows = false;
  bool inTry = false;

  Interpreter &I() const { return *interp; }

  void fail(int line, int col, const std::string &msg) {
    I().recordDiag("type error", I().file, line, col, msg);
  }

  bool isLocal(const std::string &name) const {
    for (int i = static_cast<int>(scopes.size()) - 1; i >= 0; --i) {
      if (scopes[static_cast<size_t>(i)].count(name))
        return true;
    }
    return false;
  }

  bool isSubclassOf(const std::string &child,
                    const std::string &ancestor) const {
    if (child == ancestor)
      return true;
    std::string current = child;
    std::set<std::string> seen;
    while (!current.empty()) {
      if (!seen.insert(current).second)
        break;
      auto pit = I().classParents.find(current);
      if (pit == I().classParents.end())
        break;
      if (pit->second == ancestor)
        return true;
      current = pit->second;
    }
    return false;
  }

  void checkAccess(Vis vis, const char *kind, const std::string &name,
                   const std::string &definedOn, int line, int col) {
    if (vis == Vis::Pub)
      return;
    const bool okSame = !currentSelf.empty() && currentSelf == definedOn;
    const bool okProt = vis == Vis::Protected && !currentSelf.empty() &&
                        isSubclassOf(currentSelf, definedOn);
    if (okSame || okProt)
      return;
    fail(line, col, std::string(kind) + " '" + name + "' is " + visName(vis));
  }

  void requireTry(bool throws, const std::string &name, int line, int col) {
    if (throws && !inTry)
      fail(line, col,
           "call to throwing function '" + name + "' requires 'try'");
  }

  static bool known(const std::string &ty) {
    return !ty.empty() && ty != "None" && ty != "Self" && ty != "Void";
  }

  static bool isNumeric(const std::string &ty) {
    return ty == "Int" || ty == "Float";
  }

  static bool isString(const std::string &ty) {
    return ty == "String" || ty == "Str";
  }

  static bool compatible(const std::string &a, const std::string &b) {
    if (a == b)
      return true;
    if ((a == "String" && b == "Str") || (a == "Str" && b == "String"))
      return true;
    return (a == "Int" && b == "Float") || (a == "Float" && b == "Int");
  }

  static std::string numericResult(const std::string &l,
                                   const std::string &r) {
    return (l == "Float" || r == "Float") ? "Float" : "Int";
  }

  void bind(const std::string &name, const std::string &ty) {
    if (!scopes.empty())
      scopes.back()[name] = ty;
  }

  std::string lookup(const std::string &name) const {
    for (int i = static_cast<int>(scopes.size()) - 1; i >= 0; --i) {
      auto it = scopes[static_cast<size_t>(i)].find(name);
      if (it != scopes[static_cast<size_t>(i)].end())
        return it->second;
    }
    if (name != "self" && !currentSelf.empty()) {
      std::string ft = fieldType(currentSelf, name);
      if (!ft.empty() || hasField(currentSelf, name))
        return ft;
    }
    return "";
  }

  bool hasField(const std::string &typeName, const std::string &name) const {
    std::string current = typeName;
    std::set<std::string> seen;
    while (!current.empty()) {
      if (!seen.insert(current).second)
        break;
      auto tit = I().allTypes.find(current);
      if (tit != I().allTypes.end() && tit->second) {
        for (const auto &f : tit->second->fields) {
          if (f == name)
            return true;
        }
      }
      auto pit = I().classParents.find(current);
      if (pit == I().classParents.end())
        break;
      current = pit->second;
    }
    return false;
  }

  std::string fieldType(const std::string &typeName,
                        const std::string &name) const {
    std::string current = typeName;
    std::set<std::string> seen;
    while (!current.empty()) {
      if (!seen.insert(current).second)
        break;
      auto tit = I().allTypes.find(current);
      if (tit != I().allTypes.end() && tit->second) {
        const structDecl &st = *tit->second;
        for (size_t i = 0; i < st.fields.size(); ++i) {
          if (st.fields[i] != name)
            continue;
          if (i < st.fieldTypes.size())
            return st.fieldTypes[i];
          return "";
        }
      }
      auto pit = I().classParents.find(current);
      if (pit == I().classParents.end())
        break;
      current = pit->second;
    }
    return "";
  }

  bool defined(const std::string &name) const {
    if (name == "self" || name == "super")
      return true;
    if (!lookup(name).empty() ||
        (!currentSelf.empty() && hasField(currentSelf, name)))
      return true;
    for (int i = static_cast<int>(scopes.size()) - 1; i >= 0; --i) {
      if (scopes[static_cast<size_t>(i)].count(name))
        return true;
    }
    if (I().findLocalFn(name) || I().fns.count(name))
      return true;
    if (name == "print" || name == "len" || name == "assert" ||
        name == "argv" || name == "argv_len")
      return true;
    if (I().allTypes.count(name) || I().structs.count(name) ||
        I().enums.count(name) || I().traits.count(name) ||
        I().signalArity.count(name))
      return true;
    if (I().findEnum(name) || I().findStruct(name))
      return true;
    if (I().findModuleBind(name) || isHostModule(name))
      return true;
    return false;
  }

  std::optional<std::string> tryModulePath(const Expr &e) const {
    if (e.kind == Expr::Kind::Var) {
      if (const std::string *b = I().findModuleBind(e.text))
        return *b;
      return std::nullopt;
    }
    if (e.kind != Expr::Kind::Member || e.kids.empty())
      return std::nullopt;
    std::optional<std::string> parent = tryModulePath(e.kids[0]);
    if (!parent)
      return std::nullopt;
    auto lit = I().loaded.find(*parent);
    if (lit == I().loaded.end())
      return std::nullopt;
    const bool internal = I().currentModule == *parent;
    const auto &map =
        internal ? lit->second.modules : lit->second.exportMods;
    auto it = map.find(e.text);
    if (it == map.end())
      return std::nullopt;
    return it->second;
  }

  std::string infer(const Expr &e) {
    switch (e.kind) {
    case Expr::Kind::Int:
      return "Int";
    case Expr::Kind::Float:
      return "Float";
    case Expr::Kind::String:
      return "String";
    case Expr::Kind::Bool:
      return "Bool";
    case Expr::Kind::Var: {
      std::string ty = lookup(e.text);
      if (known(ty))
        return ty;
      if (I().findLocalFn(e.text) || I().fns.count(e.text))
        return "Fn";
      if (const EnumDecl *en = I().findEnum(e.text))
        return en->name;
      return "";
    }
    case Expr::Kind::Unary:
      if (e.text == "!")
        return "Bool";
      if (e.text == "-") {
        std::string ty = infer(e.kids[0]);
        return isNumeric(ty) ? ty : "";
      }
      return "";
    case Expr::Kind::Binary: {
      std::string l = infer(e.kids[0]);
      std::string r = infer(e.kids[1]);
      if (!known(l) || !known(r))
        return "";
      if (e.text == "+" && isNumeric(l) && isNumeric(r))
        return numericResult(l, r);
      if (e.text == "+" && isString(l) && isString(r))
        return "String";
      if ((e.text == "-" || e.text == "*" || e.text == "/" ||
           e.text == "%") &&
          isNumeric(l) && isNumeric(r))
        return numericResult(l, r);
      if (e.text == "==" || e.text == "!=" || e.text == "<" ||
          e.text == ">" || e.text == "<=" || e.text == ">=" ||
          e.text == "&&" || e.text == "||")
        return "Bool";
      return "";
    }
    case Expr::Kind::Call:
      return inferCall(e.text);
    case Expr::Kind::MethodCall:
      return inferMethod(e);
    case Expr::Kind::Member: {
      if (!e.kids.empty() && e.kids[0].kind == Expr::Kind::Var) {
        if (I().findEnum(e.kids[0].text))
          return e.kids[0].text;
        std::string obj = infer(e.kids[0]);
        if (obj == "Array" || obj == "Map" || obj == "String" ||
            obj == "Str") {
          if (e.text == "len")
            return "Int";
          return "";
        }
        if (known(obj))
          return fieldType(obj, e.text);
      }
      return "";
    }
    case Expr::Kind::Index: {
      std::string obj = infer(e.kids[0]);
      if (isString(obj))
        return "String";
      return "";
    }
    case Expr::Kind::Array:
      return "Array";
    case Expr::Kind::Map:
      return "Map";
    case Expr::Kind::Range:
      return "Range";
    case Expr::Kind::StructLit:
      return e.text;
    case Expr::Kind::Try:
      return e.kids.empty() ? "" : infer(e.kids[0]);
    }
    return "";
  }

  std::string inferCall(const std::string &name) {
    if (name == "len")
      return "Int";
    if (name == "argv")
      return "String";
    if (name == "argv_len")
      return "Int";
    if (name == "print" || name == "assert")
      return "Void";
    if (FnDecl *fn = I().findLocalFn(name))
      return fn->returnType;
    if (!currentSelf.empty()) {
      auto found = I().lookupMethod(currentSelf, name);
      if (found.second)
        return found.second->returnType;
    }
    return "";
  }

  std::string inferMethod(const Expr &e) {
    const Expr &recv = e.kids[0];
    if (recv.kind == Expr::Kind::Var && isHostModule(recv.text)) {
      if (recv.text == "process" && e.text == "argv")
        return "String";
      if (recv.text == "process" && e.text == "argc")
        return "Int";
      if (recv.text == "__math") {
        if (e.text == "sin" || e.text == "cos" || e.text == "atan2" ||
            e.text == "sqrt" || e.text == "powf" || e.text == "to_float" ||
            e.text == "floor" || e.text == "ceil" || e.text == "random")
          return "Float";
        return "Int";
      }
      if (recv.text == "__str") {
        if (e.text == "length" || e.text == "find")
          return "Int";
        if (e.text == "contains" || e.text == "starts_with" ||
            e.text == "ends_with" || e.text == "is_empty")
          return "Bool";
        if (e.text == "split")
          return "Array";
        return "String";
      }
      if (recv.text == "__io") {
        if (e.text == "exists" || e.text == "remove")
          return "Bool";
        if (e.text == "read_text")
          return "String";
        if (e.text == "read_lines")
          return "Array";
        return "Void";
      }
      if (recv.text == "__uuid") {
        if (e.text == "valid")
          return "Bool";
        return "String";
      }
      return "Void";
    }
    if (recv.kind == Expr::Kind::Var && I().signalArity.count(recv.text))
      return "Void";
    if (recv.kind == Expr::Kind::Var) {
      if (const std::string *mod = I().findModuleBind(recv.text)) {
        auto lit = I().loaded.find(*mod);
        if (lit != I().loaded.end()) {
          auto eit = lit->second.exports.find(e.text);
          if (eit != lit->second.exports.end())
            return eit->second->returnType;
        }
      }
      if (I().findEnum(recv.text))
        return recv.text;
    }
    if (std::optional<std::string> mod = tryModulePath(recv)) {
      auto lit = I().loaded.find(*mod);
      if (lit != I().loaded.end()) {
        auto eit = lit->second.exports.find(e.text);
        if (eit != lit->second.exports.end())
          return eit->second->returnType;
      }
    }
    std::string obj = infer(recv);
    if (obj == "Array") {
      if (e.text == "len")
        return "Int";
      if (e.text == "pop")
        return "";
      if (e.text == "push")
        return "Void";
    } else if (obj == "Map") {
      if (e.text == "len")
        return "Int";
      if (e.text == "has")
        return "Bool";
      if (e.text == "keys")
        return "Array";
      if (e.text == "insert")
        return "Void";
      if (e.text == "remove")
        return "";
    } else if (isString(obj) && e.text == "len") {
      return "Int";
    }
    if (known(obj)) {
      auto found = I().lookupMethod(obj, e.text);
      if (found.second)
        return found.second->returnType;
    }
    if (FnDecl *fn = I().findUfcs(e.text))
      return fn->returnType;
    return "";
  }

  void checkBinop(const Expr &e) {
    std::string l = infer(e.kids[0]);
    std::string r = infer(e.kids[1]);
    if (!known(l) || !known(r))
      return;
    if (e.text == "+") {
      if ((isNumeric(l) && isNumeric(r)) || (isString(l) && isString(r)))
        return;
      fail(e.line, e.col, "cannot add " + l + " and " + r);
      return;
    }
    if (e.text == "-" || e.text == "*" || e.text == "/" || e.text == "%") {
      if (isNumeric(l) && isNumeric(r))
        return;
      std::string verb = e.text == "-"   ? "subtract"
                         : e.text == "*" ? "multiply"
                         : e.text == "/" ? "divide"
                                         : "modulo";
      fail(e.line, e.col, "cannot " + verb + " " + l + " and " + r);
      return;
    }
    if (e.text == "<" || e.text == ">" || e.text == "<=" ||
        e.text == ">=") {
      if (isNumeric(l) && isNumeric(r))
        return;
      fail(e.line, e.col, "cannot compare " + l + " and " + r);
    }
  }

  void checkCompound(const std::string &op, const std::string &lt,
                     const std::string &rt, int line, int col) {
    if (op.empty() || op == "=") {
      if (known(lt) && known(rt) && !compatible(lt, rt))
        fail(line, col, "cannot assign " + rt + " to " + lt);
      return;
    }
    if (!known(lt) || !known(rt))
      return;
    const std::string bin = op.substr(0, op.size() - 1);
    if (bin == "+") {
      if ((isNumeric(lt) && isNumeric(rt)) || (isString(lt) && isString(rt)))
        return;
      fail(line, col, "cannot add " + lt + " and " + rt);
      return;
    }
    if (bin == "-" || bin == "*" || bin == "/") {
      if (isNumeric(lt) && isNumeric(rt))
        return;
      std::string verb = bin == "-"   ? "subtract"
                         : bin == "*" ? "multiply"
                                      : "divide";
      fail(line, col, "cannot " + verb + " " + lt + " and " + rt);
    }
  }

  void checkIndex(const Expr &e) {
    std::string obj = infer(e.kids[0]);
    std::string idx = infer(e.kids[1]);
    if (!known(obj))
      return;
    if (obj == "Array" || isString(obj)) {
      if (known(idx) && idx != "Int")
        fail(e.line, e.col, "index must be Int");
      return;
    }
    if (obj == "Map") {
      if (known(idx) && !isString(idx))
        fail(e.line, e.col, "map key must be String");
      return;
    }
    std::string with = known(idx) ? idx : "unknown";
    fail(e.line, e.col, "cannot index " + obj + " with " + with);
  }

  void checkArgTypes(const FnDecl &fn, const std::vector<Expr> &args,
                     bool isMethod, int line, int col) {
    const size_t off = isMethod ? 1 : 0;
    for (size_t i = 0; i < args.size(); ++i) {
      if (i + off >= fn.paramTypes.size())
        break;
      const std::string &expect = fn.paramTypes[i + off];
      std::string got = infer(args[i]);
      if (known(expect) && known(got) && !compatible(expect, got))
        fail(line, col,
             "cannot pass " + got + " to '" + fn.name + "', expected " +
                 expect);
    }
  }

  void checkArity(const std::string &label, size_t expected, size_t got,
                  int line, int col) {
    if (expected != got)
      fail(line, col,
           label + " expected " + std::to_string(expected) + " args, got " +
               std::to_string(got));
  }

  void checkCall(const Expr &e) {
    const std::string &name = e.text;
    const size_t n = e.kids.size();
    if (name == "print")
      return;
    if (name == "len") {
      checkArity("len", 1, n, e.line, e.col);
      if (n == 1) {
        std::string ty = infer(e.kids[0]);
        if (known(ty) && ty != "Array" && ty != "Map" && !isString(ty))
          fail(e.line, e.col, "len expects Array, String, or Map");
      }
      return;
    }
    if (name == "assert") {
      checkArity("assert", 1, n, e.line, e.col);
      return;
    }
    if (name == "argv") {
      checkArity("argv", 1, n, e.line, e.col);
      return;
    }
    if (name == "argv_len") {
      checkArity("argv_len", 0, n, e.line, e.col);
      return;
    }
    if (FnDecl *fn = I().findLocalFn(name)) {
      checkArity(name, fn->params.size(), n, e.line, e.col);
      checkArgTypes(*fn, e.kids, false, e.line, e.col);
      requireTry(fn->throws, name, e.line, e.col);
      return;
    }
    if (!currentSelf.empty()) {
      auto found = I().lookupMethod(currentSelf, name);
      if (found.second) {
        const FnDecl &fn = *found.second;
        checkAccess(fn.vis, "method", name, found.first, e.line, e.col);
        const size_t expect =
            fn.params.empty() ? 0 : fn.params.size() - 1;
        checkArity(currentSelf + "." + name, expect, n, e.line, e.col);
        checkArgTypes(fn, e.kids, true, e.line, e.col);
        requireTry(fn.throws, name, e.line, e.col);
        return;
      }
    }
    fail(e.line, e.col, "unknown function '" + name + "'");
  }

  void checkHostCall(const std::string &mod, const std::string &name,
                     size_t n, int line, int col) {
    auto arity = [&](size_t expected) {
      checkArity(mod + "." + name, expected, n, line, col);
    };
    if (mod == "checks") {
      if (name == "eq" || name == "neq" || name == "eq_string") {
        arity(2);
        return;
      }
      if (name == "that" || name == "truthy") {
        arity(1);
        return;
      }
      fail(line, col, "unknown function checks." + name);
      return;
    }
    if (mod == "process") {
      if (name == "argv") {
        arity(1);
        return;
      }
      if (name == "argc") {
        arity(0);
        return;
      }
      fail(line, col, "unknown function process." + name);
      return;
    }
    if (mod == "__math") {
      if (name == "pow" || name == "powf" || name == "atan2") {
        arity(2);
        return;
      }
      if (name == "random") {
        arity(0);
        return;
      }
      if (name == "rand_int" || name == "sin" || name == "cos" ||
          name == "sqrt" || name == "to_int" || name == "to_float" ||
          name == "floor" || name == "ceil") {
        arity(1);
        return;
      }
      fail(line, col, "unknown function __math." + name);
      return;
    }
    if (mod == "__str") {
      if (name == "slice" || name == "replace") {
        arity(3);
        return;
      }
      if (name == "repeat" || name == "contains" || name == "starts_with" ||
          name == "ends_with" || name == "split" || name == "find") {
        arity(2);
        return;
      }
      if (name == "length" || name == "is_empty" || name == "upper" ||
          name == "lower" || name == "trim") {
        arity(1);
        return;
      }
      fail(line, col, "unknown function __str." + name);
      return;
    }
    if (mod == "__io") {
      if (name == "write_text") {
        arity(2);
        requireTry(true, name, line, col);
        return;
      }
      if (name == "read_text" || name == "read_lines") {
        arity(1);
        requireTry(true, name, line, col);
        return;
      }
      if (name == "exists" || name == "remove") {
        arity(1);
        return;
      }
      fail(line, col, "unknown function __io." + name);
      return;
    }
    if (mod == "__uuid") {
      if (name == "v4") {
        arity(0);
        return;
      }
      if (name == "parse") {
        arity(1);
        requireTry(true, name, line, col);
        return;
      }
      if (name == "valid") {
        arity(1);
        return;
      }
      fail(line, col, "unknown function __uuid." + name);
      return;
    }
  }

  bool checkUfcs(const Expr &e, size_t n) {
    FnDecl *fn = I().findUfcs(e.text);
    if (!fn)
      return false;
    std::vector<Expr> args(e.kids.begin() + (e.kids.empty() ? 0 : 1),
                           e.kids.end());
    checkArity(e.text, fn->params.size(), n + 1, e.line, e.col);
    if (!fn->paramTypes.empty()) {
      const std::string &expect = fn->paramTypes[0];
      std::string got = infer(e.kids[0]);
      if (known(expect) && known(got) && !compatible(expect, got))
        fail(e.line, e.col,
             "cannot pass " + got + " to '" + fn->name + "', expected " +
                 expect);
    }
    checkArgTypes(*fn, args, true, e.line, e.col);
    requireTry(fn->throws, fn->name, e.line, e.col);
    return true;
  }

  void checkValueMethod(const std::string &obj, const Expr &e, size_t n) {
    auto arity = [&](size_t expected) {
      checkArity(obj + "." + e.text, expected, n, e.line, e.col);
    };
    if (obj == "Array") {
      if (e.text == "len" || e.text == "pop") {
        arity(0);
        return;
      }
      if (e.text == "push") {
        arity(1);
        return;
      }
      if (checkUfcs(e, n))
        return;
      fail(e.line, e.col, "Array has no method '" + e.text + "'");
      return;
    }
    if (obj == "Map") {
      if (e.text == "len" || e.text == "keys") {
        arity(0);
        return;
      }
      if (e.text == "has" || e.text == "remove") {
        arity(1);
        return;
      }
      if (e.text == "insert") {
        arity(2);
        return;
      }
      if (checkUfcs(e, n))
        return;
      fail(e.line, e.col, "Map has no method '" + e.text + "'");
      return;
    }
    if (isString(obj)) {
      if (e.text == "len") {
        arity(0);
        return;
      }
      if (checkUfcs(e, n))
        return;
      fail(e.line, e.col, "String has no method '" + e.text + "'");
    }
  }

  void checkMethod(const Expr &e) {
    const Expr &recv = e.kids[0];
    const size_t n = e.kids.size() > 0 ? e.kids.size() - 1 : 0;
    std::vector<Expr> args(e.kids.begin() + (e.kids.empty() ? 0 : 1),
                           e.kids.end());
    if (recv.kind == Expr::Kind::Var && recv.text == "super") {
      if (currentSuper.empty()) {
        fail(e.line, e.col,
             "super is only valid in a method of a class that extends "
             "another");
        return;
      }
      auto found = I().lookupMethod(currentSuper, e.text);
      if (!found.second) {
        fail(e.line, e.col,
             "struct " + currentSuper + " has no method '" + e.text + "'");
        return;
      }
      const FnDecl &fn = *found.second;
      if (fn.isAbstract) {
        fail(e.line, e.col, "cannot call abstract method '" + e.text + "'");
        return;
      }
      checkAccess(fn.vis, "method", e.text, found.first, e.line, e.col);
      const size_t expect = fn.params.empty() ? 0 : fn.params.size() - 1;
      checkArity(currentSuper + "." + e.text, expect, n, e.line, e.col);
      checkArgTypes(fn, args, true, e.line, e.col);
      requireTry(fn.throws, e.text, e.line, e.col);
      return;
    }
    if (recv.kind == Expr::Kind::Var && I().signalArity.count(recv.text)) {
      if (e.text == "connect" || e.text == "disconnect") {
        checkArity("signal '" + recv.text + "' " + e.text, 1, n, e.line,
                   e.col);
        if (n == 1) {
          std::string ty = infer(e.kids[1]);
          if (known(ty) && ty != "Fn")
            fail(e.line, e.col,
                 "signal '" + recv.text + "' " + e.text +
                     " expects a function");
        }
        return;
      }
      if (e.text == "emit") {
        const size_t expect = I().signalArity[recv.text];
        if (expect != n)
          fail(e.line, e.col,
               "signal '" + recv.text + "' expected " +
                   std::to_string(expect) + " argument(s), got " +
                   std::to_string(n));
        return;
      }
      fail(e.line, e.col,
           "signal '" + recv.text + "' has no method '" + e.text + "'");
      return;
    }
    if (recv.kind == Expr::Kind::Var && isHostModule(recv.text)) {
      checkHostCall(recv.text, e.text, n, e.line, e.col);
      return;
    }
    if (recv.kind == Expr::Kind::Var) {
      if (const std::string *mod = I().findModuleBind(recv.text)) {
        auto lit = I().loaded.find(*mod);
        if (lit == I().loaded.end()) {
          fail(e.line, e.col, "unknown module '" + *mod + "'");
          return;
        }
        auto eit = lit->second.exports.find(e.text);
        if (eit == lit->second.exports.end()) {
          fail(e.line, e.col,
               "module '" + recv.text + "' has no export '" + e.text + "'");
          return;
        }
        checkArity(recv.text + "." + e.text, eit->second->params.size(), n,
                   e.line, e.col);
        checkArgTypes(*eit->second, args, false, e.line, e.col);
        requireTry(eit->second->throws, e.text, e.line, e.col);
        return;
      }
      if (const EnumDecl *en = I().findEnum(recv.text)) {
        const EnumVariant *found = nullptr;
        for (const auto &v : en->variants) {
          if (v.name == e.text) {
            found = &v;
            break;
          }
        }
        if (!found) {
          fail(e.line, e.col,
               "enum " + en->name + " has no variant '" + e.text + "'");
          return;
        }
        if (static_cast<int>(n) != found->arity)
          fail(e.line, e.col,
               en->name + "." + e.text + " takes " +
                   std::to_string(found->arity) + " argument(s)");
        return;
      }
    }
    if (std::optional<std::string> mod = tryModulePath(recv)) {
      auto lit = I().loaded.find(*mod);
      if (lit != I().loaded.end()) {
        auto eit = lit->second.exports.find(e.text);
        if (eit == lit->second.exports.end()) {
          fail(e.line, e.col,
               "module '" + *mod + "' has no export '" + e.text + "'");
          return;
        }
        checkArity(*mod + "." + e.text, eit->second->params.size(), n,
                   e.line, e.col);
        checkArgTypes(*eit->second, args, false, e.line, e.col);
        requireTry(eit->second->throws, e.text, e.line, e.col);
        return;
      }
    }
    std::string obj = infer(recv);
    if (obj == "Array" || obj == "Map" || isString(obj)) {
      checkValueMethod(isString(obj) ? "String" : obj, e, n);
      return;
    }
    if (known(obj)) {
      auto found = I().lookupMethod(obj, e.text);
      if (found.second) {
        const FnDecl &fn = *found.second;
        checkAccess(fn.vis, "method", e.text, found.first, e.line, e.col);
        const size_t expect = fn.params.empty() ? 0 : fn.params.size() - 1;
        checkArity(obj + "." + e.text, expect, n, e.line, e.col);
        checkArgTypes(fn, args, true, e.line, e.col);
        requireTry(fn.throws, e.text, e.line, e.col);
        return;
      }
      if (checkUfcs(e, n))
        return;
      fail(e.line, e.col,
           "struct " + obj + " has no method '" + e.text + "'");
      return;
    }
    checkUfcs(e, n);
  }

  void walkExpr(const Expr &e) {
    if (e.kind == Expr::Kind::Try) {
      if (e.kids.empty())
        return;
      const bool prev = inTry;
      inTry = true;
      walkExpr(e.kids[0]);
      inTry = prev;
      return;
    }
    if (e.kind == Expr::Kind::Var) {
      if (!defined(e.text))
        fail(e.line, e.col, "undefined variable '" + e.text + "'");
      else if (!isLocal(e.text) && !currentSelf.empty()) {
        auto found = I().lookupField(currentSelf, e.text);
        if (!found.first.empty())
          checkAccess(found.second, "field", e.text, found.first, e.line,
                      e.col);
      }
      return;
    }
    if (e.kind == Expr::Kind::Call) {
      for (const auto &kid : e.kids)
        walkExpr(kid);
      checkCall(e);
      return;
    }
    if (e.kind == Expr::Kind::MethodCall) {
      for (const auto &kid : e.kids)
        walkExpr(kid);
      checkMethod(e);
      return;
    }
    if (e.kind == Expr::Kind::Member) {
      for (const auto &kid : e.kids)
        walkExpr(kid);
      if (!e.kids.empty() && e.kids[0].kind == Expr::Kind::Var) {
        const std::string &recv = e.kids[0].text;
        if (const EnumDecl *en = I().findEnum(recv)) {
          bool ok = false;
          for (const auto &v : en->variants) {
            if (v.name == e.text) {
              ok = true;
              break;
            }
          }
          if (!ok)
            fail(e.line, e.col,
                 "enum " + en->name + " has no variant '" + e.text + "'");
          return;
        }
        if (tryModulePath(e) || I().findModuleBind(recv) ||
            isHostModule(recv) || I().signalArity.count(recv))
          return;
      }
      if (tryModulePath(e))
        return;
      std::string obj = e.kids.empty() ? "" : infer(e.kids[0]);
      if (obj == "Array" || obj == "Map" || isString(obj)) {
        if (e.text != "len")
          fail(e.line, e.col, obj + " has no member '" + e.text + "'");
        return;
      }
      if (known(obj) && !hasField(obj, e.text))
        fail(e.line, e.col,
             "struct " + obj + " has no field '" + e.text + "'");
      else if (known(obj)) {
        auto found = I().lookupField(obj, e.text);
        if (!found.first.empty())
          checkAccess(found.second, "field", e.text, found.first, e.line,
                      e.col);
      }
      return;
    }
    if (e.kind == Expr::Kind::Index) {
      for (const auto &kid : e.kids)
        walkExpr(kid);
      checkIndex(e);
      return;
    }
    if (e.kind == Expr::Kind::Range) {
      for (const auto &kid : e.kids)
        walkExpr(kid);
      if (e.kids.size() >= 2) {
        std::string start = infer(e.kids[0]);
        std::string end = infer(e.kids[1]);
        if (known(start) && start != "Int")
          fail(e.kids[0].line, e.kids[0].col, "range start must be Int");
        if (known(end) && end != "Int")
          fail(e.kids[1].line, e.kids[1].col, "range end must be Int");
      }
      return;
    }
    if (e.kind == Expr::Kind::StructLit) {
      for (const auto &kid : e.kids)
        walkExpr(kid);
      const structDecl *st = nullptr;
      auto tit = I().allTypes.find(e.text);
      if (tit != I().allTypes.end())
        st = tit->second;
      if (!st)
        st = I().findStruct(e.text);
      if (!st) {
        fail(e.line, e.col, "undefined struct '" + e.text + "'");
        return;
      }
      auto absIt = I().classAbstract.find(e.text);
      if (absIt != I().classAbstract.end() && absIt->second) {
        fail(e.line, e.col, "cannot construct abstract class '" + e.text + "'");
        return;
      }
      std::set<std::string> seen;
      for (const auto &field : e.names) {
        if (std::find(st->fields.begin(), st->fields.end(), field) ==
            st->fields.end())
          fail(e.line, e.col, "unknown field '" + field + "' on " + st->name);
        if (!seen.insert(field).second)
          fail(e.line, e.col,
               "duplicate field '" + field + "' on " + st->name);
      }
      auto defIt = I().fieldDefaults.find(st->name);
      for (const auto &field : e.names) {
        auto found = I().lookupField(st->name, field);
        if (!found.first.empty() && found.first != st->name)
          checkAccess(found.second, "field", field, found.first, e.line,
                      e.col);
      }
      for (const auto &field : st->fields) {
        if (std::find(e.names.begin(), e.names.end(), field) != e.names.end())
          continue;
        bool hasDef = defIt != I().fieldDefaults.end() &&
                      defIt->second.count(field);
        if (!hasDef && !st->isOptionalField(field))
          fail(e.line, e.col, "missing field '" + field + "' on " + st->name);
      }
      return;
    }
    for (const auto &kid : e.kids)
      walkExpr(kid);
    if (e.kind == Expr::Kind::Binary)
      checkBinop(e);
    if (e.kind == Expr::Kind::Unary && e.text == "-" && !e.kids.empty()) {
      std::string ty = infer(e.kids[0]);
      if (known(ty) && !isNumeric(ty))
        fail(e.line, e.col, "cannot negate " + ty);
    }
  }

  void walkStmt(const Stmt &stmt) {
    switch (stmt.kind) {
    case Stmt::Kind::Pass:
    case Stmt::Kind::Break:
    case Stmt::Kind::Continue:
      return;
    case Stmt::Kind::Throw:
      if (!currentThrows)
        fail(stmt.line, stmt.col, "throw outside throwing function");
      walkExpr(stmt.expr);
      return;
    case Stmt::Kind::Do:
      for (const auto &s : stmt.body)
        walkStmt(s);
      if (!stmt.name.empty()) {
        scopes.emplace_back();
        bind(stmt.name, "");
        for (const auto &s : stmt.elseBody)
          walkStmt(s);
        scopes.pop_back();
      }
      return;
    case Stmt::Kind::Expr:
      walkExpr(stmt.expr);
      return;
    case Stmt::Kind::Var:
    case Stmt::Kind::Const: {
      walkExpr(stmt.expr);
      std::string got = infer(stmt.expr);
      if (known(stmt.typeName) && known(got) &&
          !compatible(stmt.typeName, got))
        fail(stmt.line, stmt.col,
             "variable annotated as " + stmt.typeName +
                 " but initializer looks like " + got);
      bind(stmt.name, known(stmt.typeName) ? stmt.typeName : got);
      return;
    }
    case Stmt::Kind::Assign: {
      walkExpr(stmt.expr);
      if (!defined(stmt.name)) {
        fail(stmt.line, stmt.col, "undefined variable '" + stmt.name + "'");
        return;
      }
      if (!isLocal(stmt.name) && !currentSelf.empty()) {
        auto found = I().lookupField(currentSelf, stmt.name);
        if (!found.first.empty()) {
          checkAccess(found.second, "field", stmt.name, found.first,
                      stmt.line, stmt.col);
          if (I().isDataType(found.first) || I().isDataType(currentSelf))
            fail(stmt.line, stmt.col,
                 "cannot assign to data field '" + stmt.name + "'");
        }
      }
      std::string lt = lookup(stmt.name);
      std::string rt = infer(stmt.expr);
      checkCompound(stmt.op, lt, rt, stmt.line, stmt.col);
      return;
    }
    case Stmt::Kind::FieldAssign: {
      walkExpr(stmt.target);
      walkExpr(stmt.expr);
      std::string obj = infer(stmt.target);
      if (known(obj) && !hasField(obj, stmt.name))
        fail(stmt.line, stmt.col,
             "struct " + obj + " has no field '" + stmt.name + "'");
      else if (known(obj)) {
        auto found = I().lookupField(obj, stmt.name);
        if (!found.first.empty())
          checkAccess(found.second, "field", stmt.name, found.first,
                      stmt.line, stmt.col);
        if (I().isDataType(obj) ||
            (!found.first.empty() && I().isDataType(found.first)))
          fail(stmt.line, stmt.col,
               "cannot assign to data field '" + stmt.name + "'");
      }
      std::string lt = known(obj) ? fieldType(obj, stmt.name) : "";
      std::string rt = infer(stmt.expr);
      checkCompound(stmt.op, lt, rt, stmt.line, stmt.col);
      return;
    }
    case Stmt::Kind::IndexAssign: {
      walkExpr(stmt.target);
      walkExpr(stmt.expr);
      if (stmt.target.kids.size() >= 2)
        checkIndex(stmt.target);
      return;
    }
    case Stmt::Kind::Return:
      walkExpr(stmt.expr);
      {
        std::string got = infer(stmt.expr);
        if (known(currentReturn) && known(got) &&
            !compatible(currentReturn, got))
          fail(stmt.line, stmt.col,
               "cannot return " + got + " from " + currentReturn +
                   " function");
      }
      return;
    case Stmt::Kind::If:
      walkExpr(stmt.expr);
      for (const auto &s : stmt.body)
        walkStmt(s);
      for (const auto &s : stmt.elseBody)
        walkStmt(s);
      return;
    case Stmt::Kind::While:
      walkExpr(stmt.expr);
      for (const auto &s : stmt.body)
        walkStmt(s);
      return;
    case Stmt::Kind::For: {
      walkExpr(stmt.expr);
      scopes.emplace_back();
      std::string item;
      if (stmt.expr.kind == Expr::Kind::Array && !stmt.expr.kids.empty()) {
        std::string elem;
        bool homo = true;
        for (const auto &kid : stmt.expr.kids) {
          std::string t = infer(kid);
          if (!known(t)) {
            homo = false;
            break;
          }
          if (elem.empty())
            elem = t;
          else if (!compatible(elem, t)) {
            homo = false;
            break;
          } else if (elem == "Int" && t == "Float")
            elem = t;
        }
        if (homo)
          item = elem;
      } else {
        std::string it = infer(stmt.expr);
        if (it == "Map" || isString(it))
          item = "String";
        else if (it == "Int" || it == "Range")
          item = "Int";
      }
      bind(stmt.name, item);
      for (const auto &s : stmt.body)
        walkStmt(s);
      scopes.pop_back();
      return;
    }
    case Stmt::Kind::Match:
      walkExpr(stmt.expr);
      for (const auto &arm : stmt.arms) {
        scopes.emplace_back();
        for (const auto &b : arm.binds) {
          if (!b.empty())
            bind(b, "");
        }
        for (const auto &s : arm.body)
          walkStmt(s);
        scopes.pop_back();
      }
      return;
    }
  }

  void checkFn(const FnDecl &fn, const std::string &selfType) {
    if (fn.isAbstract)
      return;
    const std::string prevRet = currentReturn;
    const std::string prevSelf = currentSelf;
    const std::string prevSuper = currentSuper;
    const bool prevThrows = currentThrows;
    currentReturn = fn.returnType;
    currentSelf = selfType;
    currentSuper = "";
    currentThrows = fn.throws;
    if (fn.isConstexpr && fn.throws)
      fail(fn.line, 1, "constexpr function cannot throw");
    if (!selfType.empty()) {
      auto pit = I().classParents.find(selfType);
      if (pit != I().classParents.end())
        currentSuper = pit->second;
    }
    scopes.emplace_back();
    for (size_t i = 0; i < fn.params.size(); ++i) {
      std::string ty = i < fn.paramTypes.size() ? fn.paramTypes[i] : "";
      if (fn.params[i] == "self" && !selfType.empty())
        ty = selfType;
      bind(fn.params[i], ty);
    }
    for (const auto &stmt : fn.body)
      walkStmt(stmt);
    scopes.pop_back();
    currentReturn = prevRet;
    currentSelf = prevSelf;
    currentSuper = prevSuper;
    currentThrows = prevThrows;
  }

  void run() {
    for (const auto &fn : I().program.fns)
      checkFn(fn, "");
    for (const auto &type : I().typeMethods) {
      for (const auto &m : type.second)
        checkFn(*m.second, type.first);
    }
    const std::string prev = I().currentModule;
    for (auto &mod : I().loaded) {
      I().currentModule = mod.first;
      for (const auto &kv : mod.second.fns)
        checkFn(*kv.second, "");
    }
    I().currentModule = prev;
  }
};

void Interpreter::typecheckAll() {
  TypeChecker c;
  c.interp = this;
  c.run();
}
