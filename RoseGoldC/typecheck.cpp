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
  bool throwingTry = false;
  std::set<std::string> genericParams;
  std::map<std::string, std::vector<std::string>> genericBounds;

  struct BoundHit {
    const TraitMethod *m = nullptr;
    std::string trait;
    explicit operator bool() const { return m != nullptr; }
  };

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
    std::string current = typeHead(child);
    const std::string want = typeHead(ancestor);
    if (current == want)
      return true;
    std::set<std::string> seen;
    while (!current.empty()) {
      if (!seen.insert(current).second)
        break;
      auto pit = I().classParents.find(current);
      if (pit == I().classParents.end())
        break;
      if (typeHead(pit->second) == want)
        return true;
      current = typeHead(pit->second);
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
    if (!throws)
      return;
    if (inTry)
      throwingTry = true;
    else
      fail(line, col,
           "call to throwing function '" + name + "' requires 'try'");
  }

  static bool known(const std::string &ty) {
    return !ty.empty() && ty != "None" && ty != "Self" && ty != "Void";
  }

  static bool builtinHead(const std::string &head) {
    return head == "Int" || head == "Float" || head == "String" ||
           head == "Str" || head == "Bool" || head == "Void" ||
           head == "Array" || head == "Map" || head == "Range" ||
           head == "None" || head == "Self";
  }

  void checkTypeName(const std::string &ty, int line, int col) {
    if (ty.empty())
      return;
    const auto args = typeArgList(ty);
    for (const auto &a : args)
      checkTypeName(a, line, col);
    const std::string head = typeHead(ty);
    if (head.empty() || head == "_" || genericParams.count(head) ||
        builtinHead(head))
      return;
    if (I().findStruct(head) || I().findTrait(head) || I().findEnum(head) ||
        I().allTypes.count(head) || I().structs.count(head) ||
        I().traits.count(head) || I().enums.count(head))
      return;
    const StdlibExport *ex = lookupStdlibExport(head, I().file);
    if (!ex)
      return;
    std::string word = "type";
    if (ex->kind == "trait")
      word = "trait";
    else if (ex->kind == "fn")
      word = "function";
    fail(line, col,
         "undefined " + word + " '" + head + "'" +
             stdlibImportHint(head, I().file));
  }

  static bool isArrayTy(const std::string &ty) {
    return ty == "Array" ||
           (ty.size() > 6 && ty.compare(0, 6, "Array[") == 0 &&
            ty.back() == ']');
  }

  static std::string arrayElem(const std::string &ty) {
    if (ty.size() > 6 && ty.compare(0, 6, "Array[") == 0 && ty.back() == ']')
      return ty.substr(6, ty.size() - 7);
    return "";
  }

  static bool isMapTy(const std::string &ty) {
    return ty == "Map" ||
           (ty.size() > 4 && ty.compare(0, 4, "Map[") == 0 && ty.back() == ']');
  }

  static bool isNumeric(const std::string &ty) {
    return ty == "Int" || ty == "Float";
  }

  static bool isString(const std::string &ty) {
    return ty == "String" || ty == "Str";
  }

  bool isTraitType(const std::string &ty) const {
    return I().findTrait(ty) != nullptr;
  }

  bool compatible(const std::string &a, const std::string &b) {
    if (a == b)
      return true;
    if ((a == "String" && b == "Str") || (a == "Str" && b == "String"))
      return true;
    if ((a == "Int" && b == "Float") || (a == "Float" && b == "Int"))
      return true;
    if (isArrayTy(a) && isArrayTy(b)) {
      if (a == "Array" || b == "Array")
        return true;
      return compatible(arrayElem(a), arrayElem(b));
    }
    if (isMapTy(a) && isMapTy(b)) {
      auto aa = typeArgList(a);
      auto ab = typeArgList(b);
      if (aa.size() == 2 && ab.size() == 2)
        return compatible(aa[0], ab[0]) && compatible(aa[1], ab[1]);
      return true;
    }
    if (isTraitType(a)) {
      if (isTraitType(b) && compatibleTrait(a, b))
        return true;
      return implementsBound(b, a);
    }
    const std::string ha = typeHead(a);
    const std::string hb = typeHead(b);
    if (ha == hb && (a.find('[') != std::string::npos ||
                     b.find('[') != std::string::npos)) {
      auto aa = typeArgList(a);
      auto ab = typeArgList(b);
      if (aa.empty() || ab.empty())
        return true;
      if (aa.size() != ab.size())
        return false;
      for (size_t i = 0; i < aa.size(); ++i) {
        if (!compatible(aa[i], ab[i]))
          return false;
      }
      return true;
    }
    return false;
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
      if (I().lookupTypeSignal(currentSelf, name))
        return "Signal";
    }
    return "";
  }

  bool hasField(const std::string &typeName, const std::string &name) const {
    std::string current = typeHead(typeName);
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
      current = typeHead(pit->second);
    }
    return false;
  }

  std::string fieldType(const std::string &typeName,
                        const std::string &name) const {
    std::string applied = typeName;
    std::set<std::string> seen;
    while (!applied.empty()) {
      const std::string current = typeHead(applied);
      if (!seen.insert(current).second)
        break;
      auto tit = I().allTypes.find(current);
      if (tit != I().allTypes.end() && tit->second) {
        const structDecl &st = *tit->second;
        for (size_t i = 0; i < st.fields.size(); ++i) {
          if (st.fields[i] != name)
            continue;
          if (i < st.fieldTypes.size()) {
            const auto env =
                typeEnvFrom(st.typeParams, typeArgList(applied));
            return substType(st.fieldTypes[i], env);
          }
          return "";
        }
      }
      applied = appliedParent(I().classParents, I().allTypes, applied);
    }
    return "";
  }

  bool defined(const std::string &name) const {
    if (name == "self" || name == "super")
      return true;
    if (!lookup(name).empty() ||
        (!currentSelf.empty() &&
         (hasField(currentSelf, name) ||
          I().lookupTypeSignal(currentSelf, name).has_value())))
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

  std::string inferArray(const Expr &e) {
    if (e.kids.empty())
      return "Array";
    std::string elem;
    for (const auto &kid : e.kids) {
      std::string t = infer(kid);
      if (!known(t))
        return "Array";
      if (elem.empty())
        elem = t;
      else if (elem == "Int" && t == "Float")
        elem = t;
      else if (!(elem == t || compatible(elem, t)))
        return "Array";
    }
    return "Array[" + elem + "]";
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
      return inferCallExpr(e);
    case Expr::Kind::Lambda:
      return "Fn";
    case Expr::Kind::MethodCall:
      return inferMethod(e);
    case Expr::Kind::Member: {
      if (!e.kids.empty() && e.kids[0].kind == Expr::Kind::Var &&
          I().findEnum(e.kids[0].text))
        return e.kids[0].text;
      std::string obj = e.kids.empty() ? "" : infer(e.kids[0]);
      if (isArrayTy(obj) || isMapTy(obj) || obj == "String" || obj == "Str") {
        if (e.text == "len")
          return "Int";
        return "";
      }
      if (known(obj)) {
        if (I().lookupTypeSignal(obj, e.text))
          return "Signal";
        return fieldType(obj, e.text);
      }
      return "";
    }
    case Expr::Kind::Index: {
      std::string obj = infer(e.kids[0]);
      if (isString(obj))
        return "String";
      if (isArrayTy(obj))
        return arrayElem(obj);
      return "";
    }
    case Expr::Kind::Array:
      return inferArray(e);
    case Expr::Kind::Map:
      return "Map";
    case Expr::Kind::Range:
      return "Range";
    case Expr::Kind::StructLit:
      return inferStructLit(e);
    case Expr::Kind::Try:
      return e.kids.empty() ? "" : infer(e.kids[0]);
    }
    return "";
  }

  std::string inferStructLit(const Expr &e) {
    const structDecl *st = I().findStruct(e.text);
    if (!st)
      return e.text;
    if (st->typeParams.empty())
      return e.text;
    if (!e.typeArgs.empty())
      return typeApply(e.text, e.typeArgs);
    std::map<std::string, std::string> env;
    auto prev = genericParams;
    for (const auto &p : st->typeParams)
      genericParams.insert(p.name);
    for (size_t i = 0; i < e.names.size() && i < e.kids.size(); ++i)
      unifyType(st->typeOfField(e.names[i]), infer(e.kids[i]), env);
    genericParams = std::move(prev);
    std::vector<std::string> args;
    args.reserve(st->typeParams.size());
    for (const auto &p : st->typeParams) {
      auto it = env.find(p.name);
      if (it == env.end() || it->second == p.name || !known(it->second))
        return e.text;
      args.push_back(it->second);
    }
    return typeApply(e.text, args);
  }

  std::string inferCallExpr(const Expr &e) {
    if (e.text.empty() && !e.kids.empty()) {
      if (e.kids[0].kind == Expr::Kind::Lambda && e.kids[0].lambda) {
        const FnDecl &fn = *e.kids[0].lambda;
        if (fn.typeParams.empty())
          return fn.returnType;
        std::vector<Expr> args(e.kids.begin() + 1, e.kids.end());
        auto env =
            inferEnv(fn.typeParams, e.typeArgs, fn.paramTypes, args);
        return substType(fn.returnType, env);
      }
      return "";
    }
    if (e.text == "len")
      return "Int";
    if (e.text == "argv")
      return "String";
    if (e.text == "argv_len")
      return "Int";
    if (e.text == "print" || e.text == "assert")
      return "Void";
    if (FnDecl *fn = I().findLocalFn(e.text)) {
      if (fn->typeParams.empty())
        return fn->returnType;
      auto env =
          inferEnv(fn->typeParams, e.typeArgs, fn->paramTypes, e.kids);
      return substType(fn->returnType, env);
    }
    return inferCall(e.text);
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
          return "Array[String]";
        return "String";
      }
      if (recv.text == "__io") {
        if (e.text == "exists" || e.text == "remove")
          return "Bool";
        if (e.text == "read_text")
          return "String";
        if (e.text == "read_lines")
          return "Array[String]";
        return "Void";
      }
      if (recv.text == "__uuid") {
        if (e.text == "valid")
          return "Bool";
        return "String";
      }
      if (recv.text == "__time") {
        if (e.text == "now")
          return "Int";
        return "Void";
      }
      if (recv.text == "__path")
        return "String";
      if (recv.text == "__json") {
        if (e.text == "valid")
          return "Bool";
        if (e.text == "stringify")
          return "String";
        return "";
      }
      if (recv.text == "__ui") {
        if (e.text == "open")
          return "Int";
        if (e.text == "title" || e.text == "backend")
          return "String";
        if (e.text == "alive" || e.text == "poll" || e.text == "mouse_down" ||
            e.text == "take_click")
          return "Bool";
        if (e.text == "width" || e.text == "height" || e.text == "count" ||
            e.text == "mouse_x" || e.text == "mouse_y")
          return "Int";
        return "Void";
      }
      return "Void";
    }
    if (recv.kind == Expr::Kind::Var && I().signalArity.count(recv.text))
      return "Void";
    if (recv.kind == Expr::Kind::Var && !currentSelf.empty() &&
        I().lookupTypeSignal(currentSelf, recv.text))
      return "Void";
    std::string recvTy = infer(recv);
    if (recvTy == "Signal")
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
    if (isArrayTy(obj)) {
      if (e.text == "len")
        return "Int";
      if (e.text == "pop")
        return arrayElem(obj);
      if (e.text == "push")
        return "Void";
    } else if (isMapTy(obj)) {
      if (e.text == "len")
        return "Int";
      if (e.text == "has")
        return "Bool";
      if (e.text == "keys")
        return "Array[String]";
      if (e.text == "insert")
        return "Void";
      if (e.text == "remove")
        return "";
    } else if (isString(obj) && e.text == "len") {
      return "Int";
    }
    if (known(obj)) {
      if (BoundHit hit = traitObjectMethod(obj, e.text); hit.m)
        return substType(hit.m->returnType, traitEnv(hit.trait));
      if (BoundHit hit = boundMethod(obj, e.text); hit.m)
        return substType(hit.m->returnType, traitEnv(hit.trait));
      auto found = I().lookupMethod(obj, e.text);
      if (found.second) {
        auto env = envForApplied(recvApplied(obj, found.first));
        if (!found.second->typeParams.empty()) {
          std::vector<std::string> patterns;
          for (size_t i = 1; i < found.second->paramTypes.size(); ++i)
            patterns.push_back(found.second->paramTypes[i]);
          std::vector<Expr> args(e.kids.begin() + (e.kids.size() > 0 ? 1 : 0),
                                 e.kids.end());
          auto more = inferEnv(found.second->typeParams, e.typeArgs, patterns,
                               args);
          env.insert(more.begin(), more.end());
        }
        return substType(found.second->returnType, env);
      }
    }
    if (FnDecl *fn = I().findUfcs(e.text)) {
      if (fn->typeParams.empty())
        return fn->returnType;
      std::vector<std::string> patterns = fn->paramTypes;
      std::vector<Expr> vals;
      vals.push_back(e.kids.empty() ? Expr{} : e.kids[0]);
      for (size_t i = 1; i < e.kids.size(); ++i)
        vals.push_back(e.kids[i]);
      auto env = inferEnv(fn->typeParams, e.typeArgs, patterns, vals);
      return substType(fn->returnType, env);
    }
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
    if (isArrayTy(obj) || isString(obj)) {
      if (known(idx) && idx != "Int")
        fail(e.line, e.col, "index must be Int");
      return;
    }
    if (isMapTy(obj)) {
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
      checkArrayElems(expect, args[i], line, col);
    }
  }

  void checkArity(const std::string &label, size_t expected, size_t got,
                  int line, int col) {
    if (expected != got)
      fail(line, col,
           label + " expected " + std::to_string(expected) + " args, got " +
               std::to_string(got));
  }

  bool unifyType(const std::string &pattern, const std::string &got,
                 std::map<std::string, std::string> &env) {
    if (pattern.empty() || !known(got))
      return true;
    const std::string ph = typeHead(pattern);
    const auto pa = typeArgList(pattern);
    if (pa.empty() && genericParams.count(ph)) {
      auto it = env.find(ph);
      if (it == env.end() || it->second == ph) {
        env[ph] = got;
        return true;
      }
      return compatible(it->second, got);
    }
    if (isArrayTy(pattern) && isArrayTy(got)) {
      const std::string pe = arrayElem(pattern);
      const std::string ge = arrayElem(got);
      if (pe.empty() || ge.empty())
        return true;
      return unifyType(pe, ge, env);
    }
    if (isMapTy(pattern) && isMapTy(got))
      return true;
    const std::string gh = typeHead(got);
    const auto ga = typeArgList(got);
    if (ph != gh)
      return compatible(pattern, got);
    if (pa.empty() || ga.empty())
      return true;
    if (pa.size() != ga.size())
      return false;
    for (size_t i = 0; i < pa.size(); ++i) {
      if (!unifyType(pa[i], ga[i], env))
        return false;
    }
    return true;
  }

  std::map<std::string, std::string>
  inferEnv(const std::vector<TypeParam> &params,
           const std::vector<std::string> &explicitArgs,
           const std::vector<std::string> &patterns,
           const std::vector<Expr> &vals) {
    std::map<std::string, std::string> env;
    std::set<std::string> prevParams = genericParams;
    for (const auto &p : params)
      genericParams.insert(p.name);
    if (!explicitArgs.empty())
      env = typeEnvFrom(params, explicitArgs);
    const size_t n = std::min(patterns.size(), vals.size());
    for (size_t i = 0; i < n; ++i)
      unifyType(patterns[i], infer(vals[i]), env);
    genericParams = std::move(prevParams);
    return env;
  }

  std::string recvApplied(const std::string &obj,
                          const std::string &definedOn) const {
    std::string applied = obj;
    std::set<std::string> seen;
    while (!applied.empty() && seen.insert(typeHead(applied)).second) {
      if (typeHead(applied) == typeHead(definedOn))
        return applied;
      applied = appliedParent(I().classParents, I().allTypes, applied);
    }
    return definedOn;
  }

  std::map<std::string, std::string>
  envForApplied(const std::string &applied) const {
    const structDecl *st = I().findStruct(applied);
    if (!st) {
      auto tit = I().allTypes.find(typeHead(applied));
      if (tit != I().allTypes.end())
        st = tit->second;
    }
    return typeEnvFrom(st ? st->typeParams : std::vector<TypeParam>{},
                       typeArgList(applied));
  }

  std::map<std::string, std::string>
  traitEnv(const std::string &applied) const {
    const TraitDecl *tr = I().findTrait(applied);
    return typeEnvFrom(tr ? tr->typeParams : std::vector<TypeParam>{},
                       typeArgList(applied));
  }

  bool compatibleTrait(const std::string &impl,
                       const std::string &bound) {
    if (typeHead(impl) != typeHead(bound))
      return false;
    auto ia = typeArgList(impl);
    auto ba = typeArgList(bound);
    if (ia.empty() || ba.empty())
      return true;
    if (ia.size() != ba.size())
      return false;
    for (size_t i = 0; i < ia.size(); ++i) {
      if (!compatible(ia[i], ba[i]))
        return false;
    }
    return true;
  }

  bool implementsBound(const std::string &concrete,
                       const std::string &bound) {
    if (isTraitType(concrete) && compatibleTrait(concrete, bound))
      return true;
    std::string applied = concrete;
    std::set<std::string> seen;
    while (!applied.empty() && seen.insert(typeHead(applied)).second) {
      const std::string head = typeHead(applied);
      for (const auto &im : I().traitImpls) {
        if (typeHead(im.typeName) != head)
          continue;
        auto env = envForApplied(applied);
        auto prev = genericParams;
        for (const auto &p : im.typeParams)
          genericParams.insert(p.name);
        unifyType(im.typeName, applied, env);
        genericParams = std::move(prev);
        if (compatibleTrait(substType(im.traitName, env), bound))
          return true;
      }
      applied = appliedParent(I().classParents, I().allTypes, applied);
    }
    return false;
  }

  void checkTraitBound(const std::string &bound, int line, int col) {
    const std::string head = typeHead(bound);
    const TraitDecl *tr = I().findTrait(head);
    if (!tr) {
      fail(line, col,
           "undefined trait '" + head + "'" + stdlibImportHint(head, I().file));
      return;
    }
    const auto args = typeArgList(bound);
    if (args.empty())
      return;
    if (tr->typeParams.empty())
      fail(line, col, "'" + head + "' does not take type arguments");
    else if (args.size() != tr->typeParams.size())
      fail(line, col,
           "'" + head + "' expected " +
               std::to_string(tr->typeParams.size()) +
               " type argument(s), got " + std::to_string(args.size()));
  }

  void checkTypeArgCount(const std::vector<TypeParam> &params,
                         const std::vector<std::string> &args, int line,
                         int col, const std::string &who) {
    if (args.empty())
      return;
    if (params.empty()) {
      fail(line, col, "'" + who + "' does not take type arguments");
      return;
    }
    if (args.size() != params.size())
      fail(line, col,
           "'" + who + "' expected " + std::to_string(params.size()) +
               " type argument(s), got " + std::to_string(args.size()));
  }

  void checkBounds(const std::vector<TypeParam> &params,
                   const std::map<std::string, std::string> &env, int line,
                   int col) {
    for (const auto &p : params) {
      auto eit = env.find(p.name);
      if (eit == env.end() || eit->second == p.name || !known(eit->second))
        continue;
      for (const auto &bound : p.bounds) {
        if (implementsBound(eit->second, bound))
          continue;
        if (genericParams.count(typeHead(eit->second))) {
          auto git = genericBounds.find(typeHead(eit->second));
          bool ok = false;
          if (git != genericBounds.end()) {
            for (const auto &b : git->second) {
              if (compatibleTrait(b, bound)) {
                ok = true;
                break;
              }
            }
          }
          if (ok)
            continue;
        }
        fail(line, col,
             "type " + eit->second + " does not implement " + bound);
      }
    }
  }

  BoundHit boundMethod(const std::string &typeName,
                       const std::string &name) const {
    auto it = genericBounds.find(typeHead(typeName));
    if (it == genericBounds.end())
      return {};
    for (const auto &traitName : it->second) {
      const TraitDecl *tr = I().findTrait(traitName);
      if (!tr)
        continue;
      for (const auto &m : tr->methods) {
        if (m.name == name)
          return BoundHit{&m, traitName};
      }
    }
    return {};
  }

  BoundHit traitObjectMethod(const std::string &typeName,
                             const std::string &name) const {
    const TraitDecl *tr = I().findTrait(typeName);
    if (!tr)
      return {};
    for (const auto &m : tr->methods) {
      if (m.name == name)
        return BoundHit{&m, typeName};
    }
    return {};
  }

  void checkArrayElems(const std::string &expectTy, const Expr &e, int line,
                       int col) {
    if (!isArrayTy(expectTy) || e.kind != Expr::Kind::Array)
      return;
    const std::string elem = arrayElem(expectTy);
    if (!known(elem))
      return;
    for (const auto &kid : e.kids) {
      std::string got = infer(kid);
      if (known(got) && !compatible(elem, got))
        fail(line, col,
             "cannot pass " + got + " to " + expectTy + ", expected " +
                 elem);
    }
  }

  void checkBoundArgs(const BoundHit &hit, const std::string &recv,
                      const std::vector<Expr> &args, int line, int col) {
    const size_t expect =
        hit.m->params.empty() ? 0 : hit.m->params.size() - 1;
    checkArity(recv + "." + hit.m->name, expect, args.size(), line, col);
    auto env = traitEnv(hit.trait);
    for (size_t i = 0; i < args.size(); ++i) {
      if (i + 1 >= hit.m->paramTypes.size())
        break;
      std::string expectTy = substType(hit.m->paramTypes[i + 1], env);
      std::string got = infer(args[i]);
      if (known(expectTy) && known(got) && !compatible(expectTy, got))
        fail(line, col,
             "cannot pass " + got + " to '" + hit.m->name + "', expected " +
                 expectTy);
    }
    requireTry(hit.m->throws, hit.m->name, line, col);
  }

  void mergeMethodEnv(const FnDecl &fn, const Expr &e,
                      const std::vector<Expr> &args,
                      std::map<std::string, std::string> &env) {
    if (fn.typeParams.empty()) {
      if (!e.typeArgs.empty())
        fail(e.line, e.col,
             "'" + fn.name + "' does not take type arguments");
      return;
    }
    checkTypeArgCount(fn.typeParams, e.typeArgs, e.line, e.col, fn.name);
    std::vector<std::string> patterns;
    for (size_t i = 1; i < fn.paramTypes.size(); ++i)
      patterns.push_back(fn.paramTypes[i]);
    auto more = inferEnv(fn.typeParams, e.typeArgs, patterns, args);
    env.insert(more.begin(), more.end());
    checkBounds(fn.typeParams, env, e.line, e.col);
  }

  void checkArgTypesEnv(const FnDecl &fn, const std::vector<Expr> &args,
                        bool isMethod, int line, int col,
                        const std::map<std::string, std::string> &env) {
    const size_t off = isMethod ? 1 : 0;
    for (size_t i = 0; i < args.size(); ++i) {
      if (i + off >= fn.paramTypes.size())
        break;
      std::string expect = substType(fn.paramTypes[i + off], env);
      std::string got = infer(args[i]);
      if (known(expect) && known(got) && !compatible(expect, got))
        fail(line, col,
             "cannot pass " + got + " to '" + fn.name + "', expected " +
                 expect);
      checkArrayElems(expect, args[i], line, col);
    }
  }

  void checkCall(const Expr &e) {
    const std::string &name = e.text;
    if (e.text.empty()) {
      if (!e.kids.empty()) {
        if (e.kids[0].kind == Expr::Kind::Lambda && e.kids[0].lambda) {
          const FnDecl &fn = *e.kids[0].lambda;
          std::vector<Expr> args(e.kids.begin() + 1, e.kids.end());
          checkArity("<fn>", fn.params.size(), args.size(), e.line, e.col);
          checkTypeArgCount(fn.typeParams, e.typeArgs, e.line, e.col,
                            "<fn>");
          if (!fn.typeParams.empty()) {
            auto env =
                inferEnv(fn.typeParams, e.typeArgs, fn.paramTypes, args);
            checkBounds(fn.typeParams, env, e.line, e.col);
            checkArgTypesEnv(fn, args, false, e.line, e.col, env);
          } else {
            if (!e.typeArgs.empty())
              fail(e.line, e.col, "'<fn>' does not take type arguments");
            checkArgTypes(fn, args, false, e.line, e.col);
          }
          requireTry(fn.throws, "<fn>", e.line, e.col);
          return;
        }
        std::string ty = infer(e.kids[0]);
        if (known(ty) && ty != "Fn")
          fail(e.line, e.col, "can only call a function");
      }
      return;
    }
    if (lookup(name) == "Fn")
      return;
    {
      std::string ty = lookup(name);
      if (known(ty) && ty != "Fn") {
        fail(e.line, e.col, "can only call a function");
        return;
      }
    }
    const size_t n = e.kids.size();
    if (name == "print")
      return;
    if (name == "len") {
      checkArity("len", 1, n, e.line, e.col);
      if (n == 1) {
        std::string ty = infer(e.kids[0]);
        if (known(ty) && !isArrayTy(ty) && !isMapTy(ty) && !isString(ty))
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
      checkTypeArgCount(fn->typeParams, e.typeArgs, e.line, e.col, name);
      if (!fn->typeParams.empty()) {
        auto env =
            inferEnv(fn->typeParams, e.typeArgs, fn->paramTypes, e.kids);
        checkBounds(fn->typeParams, env, e.line, e.col);
        checkArgTypesEnv(*fn, e.kids, false, e.line, e.col, env);
      } else {
        if (!e.typeArgs.empty())
          fail(e.line, e.col, "'" + name + "' does not take type arguments");
        checkArgTypes(*fn, e.kids, false, e.line, e.col);
      }
      requireTry(fn->throws, name, e.line, e.col);
      return;
    }
    if (!currentSelf.empty()) {
      if (BoundHit hit = boundMethod(currentSelf, name)) {
        checkBoundArgs(hit, currentSelf, e.kids, e.line, e.col);
        return;
      }
      auto found = I().lookupMethod(currentSelf, name);
      if (found.second) {
        const FnDecl &fn = *found.second;
        checkAccess(fn.vis, "method", name, found.first, e.line, e.col);
        const size_t expect =
            fn.params.empty() ? 0 : fn.params.size() - 1;
        checkArity(currentSelf + "." + name, expect, n, e.line, e.col);
        auto env = envForApplied(recvApplied(currentSelf, found.first));
        mergeMethodEnv(fn, e, e.kids, env);
        checkArgTypesEnv(fn, e.kids, true, e.line, e.col, env);
        requireTry(fn.throws, name, e.line, e.col);
        return;
      }
    }
    fail(e.line, e.col,
         "unknown function '" + name + "'" + stdlibImportHint(name, I().file));
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
    if (mod == "__time") {
      if (name == "now") {
        arity(0);
        return;
      }
      if (name == "sleep") {
        arity(1);
        return;
      }
      fail(line, col, "unknown function __time." + name);
      return;
    }
    if (mod == "__path") {
      if (name == "join") {
        arity(2);
        return;
      }
      if (name == "parent" || name == "stem") {
        arity(1);
        return;
      }
      fail(line, col, "unknown function __path." + name);
      return;
    }
    if (mod == "__json") {
      if (name == "parse") {
        arity(1);
        requireTry(true, name, line, col);
        return;
      }
      if (name == "valid" || name == "stringify") {
        arity(1);
        return;
      }
      fail(line, col, "unknown function __json." + name);
      return;
    }
    if (mod == "__ui") {
      if (name == "open") {
        arity(4);
        requireTry(true, name, line, col);
        return;
      }
      if (name == "fill") {
        arity(6);
        return;
      }
      if (name == "text") {
        arity(5);
        return;
      }
      if (name == "text_width") {
        arity(1);
        return;
      }
      if (name == "set_size" || name == "feed_click") {
        arity(3);
        return;
      }
      if (name == "set_title" || name == "clear" || name == "set_frame") {
        arity(2);
        return;
      }
      if (name == "close" || name == "show" || name == "hide" ||
          name == "poll" || name == "alive" || name == "title" ||
          name == "width" || name == "height" || name == "present" ||
          name == "mouse_x" || name == "mouse_y" || name == "mouse_down" ||
          name == "take_click") {
        arity(1);
        return;
      }
      if (name == "run" || name == "count" || name == "backend" ||
          name == "wait" || name == "font_height") {
        arity(0);
        return;
      }
      fail(line, col, "unknown function __ui." + name);
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
    checkTypeArgCount(fn->typeParams, e.typeArgs, e.line, e.col, fn->name);
    if (!fn->typeParams.empty()) {
      std::vector<Expr> vals;
      if (!e.kids.empty())
        vals.push_back(e.kids[0]);
      for (const auto &a : args)
        vals.push_back(a);
      auto env =
          inferEnv(fn->typeParams, e.typeArgs, fn->paramTypes, vals);
      checkBounds(fn->typeParams, env, e.line, e.col);
      if (!fn->paramTypes.empty() && !e.kids.empty()) {
        std::string expect = substType(fn->paramTypes[0], env);
        std::string got = infer(e.kids[0]);
        if (known(expect) && known(got) && !compatible(expect, got))
          fail(e.line, e.col,
               "cannot pass " + got + " to '" + fn->name + "', expected " +
                   expect);
      }
      checkArgTypesEnv(*fn, args, true, e.line, e.col, env);
    } else {
      if (!fn->paramTypes.empty()) {
        const std::string &expect = fn->paramTypes[0];
        std::string got = infer(e.kids[0]);
        if (known(expect) && known(got) && !compatible(expect, got))
          fail(e.line, e.col,
               "cannot pass " + got + " to '" + fn->name + "', expected " +
                   expect);
      }
      checkArgTypes(*fn, args, true, e.line, e.col);
    }
    requireTry(fn->throws, fn->name, e.line, e.col);
    return true;
  }

  void checkValueMethod(const std::string &obj, const Expr &e, size_t n) {
    auto arity = [&](size_t expected) {
      checkArity(obj + "." + e.text, expected, n, e.line, e.col);
    };
    if (isArrayTy(obj)) {
      if (e.text == "len" || e.text == "pop") {
        arity(0);
        return;
      }
      if (e.text == "push") {
        arity(1);
        if (n == 1 && e.kids.size() >= 2) {
          const std::string elem = arrayElem(obj);
          const std::string got = infer(e.kids[1]);
          if (known(elem) && known(got) && !compatible(elem, got))
            fail(e.line, e.col,
                 "cannot pass " + got + " to 'push', expected " + elem);
          if (n == 1 && e.kids.size() >= 2)
            checkArrayElems(elem, e.kids[1], e.line, e.col);
        }
        return;
      }
      if (checkUfcs(e, n))
        return;
      fail(e.line, e.col, "Array has no method '" + e.text + "'");
      return;
    }
    if (isMapTy(obj)) {
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
      auto env = envForApplied(recvApplied(currentSuper, found.first));
      mergeMethodEnv(fn, e, args, env);
      checkArgTypesEnv(fn, args, true, e.line, e.col, env);
      requireTry(fn.throws, e.text, e.line, e.col);
      return;
    }
    auto checkSig = [&](const std::string &signal, std::size_t expect) {
      if (e.text == "connect" || e.text == "disconnect") {
        checkArity("signal '" + signal + "' " + e.text, 1, n, e.line, e.col);
        if (n == 1) {
          std::string ty = infer(e.kids[1]);
          if (known(ty) && ty != "Fn")
            fail(e.line, e.col,
                 "signal '" + signal + "' " + e.text +
                     " expects a function");
        }
        return;
      }
      if (e.text == "emit" || e.text == "emit_deferred") {
        if (expect != n)
          fail(e.line, e.col,
               "signal '" + signal + "' expected " + std::to_string(expect) +
                   " argument(s), got " + std::to_string(n));
        return;
      }
      fail(e.line, e.col,
           "signal '" + signal + "' has no method '" + e.text + "'");
    };
    if (recv.kind == Expr::Kind::Var) {
      if (!currentSelf.empty()) {
        if (auto arity = I().lookupTypeSignal(currentSelf, recv.text)) {
          checkSig(recv.text, *arity);
          return;
        }
      }
      if (I().signalArity.count(recv.text)) {
        checkSig(recv.text, I().signalArity[recv.text]);
        return;
      }
    }
    if (recv.kind == Expr::Kind::Member && !recv.kids.empty()) {
      std::string objTy = infer(recv.kids[0]);
      if (known(objTy)) {
        if (auto arity = I().lookupTypeSignal(objTy, recv.text)) {
          checkSig(recv.text, *arity);
          return;
        }
      }
    }
    if (infer(recv) == "Signal") {
      if (e.text == "connect" || e.text == "disconnect") {
        checkArity("signal " + e.text, 1, n, e.line, e.col);
        return;
      }
      if (e.text == "emit" || e.text == "emit_deferred")
        return;
      fail(e.line, e.col, "signal has no method '" + e.text + "'");
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
    if (isArrayTy(obj) || isMapTy(obj) || isString(obj)) {
      checkValueMethod(isString(obj) ? "String" : obj, e, n);
      return;
    }
    if (known(obj)) {
      if (BoundHit hit = traitObjectMethod(obj, e.text)) {
        checkBoundArgs(hit, obj, args, e.line, e.col);
        return;
      }
      if (BoundHit hit = boundMethod(obj, e.text)) {
        checkBoundArgs(hit, obj, args, e.line, e.col);
        return;
      }
      auto found = I().lookupMethod(obj, e.text);
      if (found.second) {
        const FnDecl &fn = *found.second;
        checkAccess(fn.vis, "method", e.text, found.first, e.line, e.col);
        const size_t expect = fn.params.empty() ? 0 : fn.params.size() - 1;
        checkArity(obj + "." + e.text, expect, n, e.line, e.col);
        auto env = envForApplied(recvApplied(obj, found.first));
        mergeMethodEnv(fn, e, args, env);
        checkArgTypesEnv(fn, args, true, e.line, e.col, env);
        requireTry(fn.throws, e.text, e.line, e.col);
        return;
      }
      if (checkUfcs(e, n))
        return;
      fail(e.line, e.col,
           (isTraitType(obj) ? std::string("trait ") : std::string("struct ")) +
               obj + " has no method '" + e.text + "'");
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
    if (e.kind == Expr::Kind::Lambda) {
      if (e.lambda)
        checkFn(*e.lambda, currentSelf);
      return;
    }
    if (e.kind == Expr::Kind::Var) {
      if (!defined(e.text))
        fail(e.line, e.col,
             "undefined variable '" + e.text + "'" +
                 stdlibImportHint(e.text, I().file));
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
      if (isArrayTy(obj) || isMapTy(obj) || isString(obj)) {
        if (e.text != "len")
          fail(e.line, e.col,
               (isArrayTy(obj) ? std::string("Array")
                : isMapTy(obj) ? std::string("Map")
                               : obj) +
                   " has no member '" + e.text + "'");
        return;
      }
      if (known(obj) && I().lookupTypeSignal(obj, e.text))
        return;
      if (known(obj) && isTraitType(obj)) {
        fail(e.line, e.col, "trait " + obj + " has no field '" + e.text + "'");
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
        if (I().findTrait(e.text))
          fail(e.line, e.col, "cannot construct trait '" + e.text + "'");
        else
          fail(e.line, e.col,
               "undefined struct '" + e.text + "'" +
                   stdlibImportHint(e.text, I().file));
        return;
      }
      auto absIt = I().classAbstract.find(e.text);
      if (absIt != I().classAbstract.end() && absIt->second) {
        fail(e.line, e.col, "cannot construct abstract class '" + e.text + "'");
        return;
      }
      checkTypeArgCount(st->typeParams, e.typeArgs, e.line, e.col, st->name);
      std::map<std::string, std::string> env;
      if (!st->typeParams.empty()) {
        auto prev = genericParams;
        for (const auto &p : st->typeParams)
          genericParams.insert(p.name);
        if (!e.typeArgs.empty())
          env = typeEnvFrom(st->typeParams, e.typeArgs);
        for (size_t i = 0; i < e.names.size() && i < e.kids.size(); ++i)
          unifyType(st->typeOfField(e.names[i]), infer(e.kids[i]), env);
        genericParams = std::move(prev);
        checkBounds(st->typeParams, env, e.line, e.col);
      }
      std::set<std::string> seen;
      for (size_t i = 0; i < e.names.size(); ++i) {
        const auto &field = e.names[i];
        if (std::find(st->fields.begin(), st->fields.end(), field) ==
            st->fields.end())
          fail(e.line, e.col, "unknown field '" + field + "' on " + st->name);
        if (!seen.insert(field).second)
          fail(e.line, e.col,
               "duplicate field '" + field + "' on " + st->name);
        if (i < e.kids.size()) {
          std::string expect = substType(st->typeOfField(field), env);
          std::string got = infer(e.kids[i]);
          if (known(expect) && known(got) && !compatible(expect, got))
            fail(e.line, e.col,
                 "cannot assign " + got + " to field '" + field +
                     "', expected " + expect);
          if (i < e.kids.size())
            checkArrayElems(expect, e.kids[i], e.line, e.col);
        }
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
      if (!stmt.typeName.empty()) {
        checkTypeName(stmt.typeName, stmt.line, stmt.col);
        if (const structDecl *st = I().findStruct(stmt.typeName))
          checkTypeArgCount(st->typeParams, typeArgList(stmt.typeName),
                            stmt.line, stmt.col, typeHead(stmt.typeName));
        else if (const TraitDecl *tr = I().findTrait(stmt.typeName))
          checkTypeArgCount(tr->typeParams, typeArgList(stmt.typeName),
                            stmt.line, stmt.col, typeHead(stmt.typeName));
      }
      if (known(stmt.typeName) && known(got) &&
          !compatible(stmt.typeName, got))
        fail(stmt.line, stmt.col,
             "variable annotated as " + stmt.typeName +
                 " but initializer looks like " + got);
      if (!stmt.typeName.empty())
        checkArrayElems(stmt.typeName, stmt.expr, stmt.line, stmt.col);
      bind(stmt.name, known(stmt.typeName) ? stmt.typeName : got);
      return;
    }
    case Stmt::Kind::Assign: {
      walkExpr(stmt.expr);
      if (!defined(stmt.name)) {
        fail(stmt.line, stmt.col,
             "undefined variable '" + stmt.name + "'" +
                 stdlibImportHint(stmt.name, I().file));
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
      if (stmt.op.empty() || stmt.op == "=")
        checkArrayElems(lt, stmt.expr, stmt.line, stmt.col);
      return;
    }
    case Stmt::Kind::FieldAssign: {
      walkExpr(stmt.target);
      walkExpr(stmt.expr);
      std::string obj = infer(stmt.target);
      if (known(obj) && isTraitType(obj))
        fail(stmt.line, stmt.col,
             "trait " + obj + " has no field '" + stmt.name + "'");
      else if (known(obj) && !hasField(obj, stmt.name))
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
      if (stmt.op.empty() || stmt.op == "=")
        checkArrayElems(lt, stmt.expr, stmt.line, stmt.col);
      return;
    }
    case Stmt::Kind::IndexAssign: {
      walkExpr(stmt.target);
      walkExpr(stmt.expr);
      if (stmt.target.kids.size() >= 2)
        checkIndex(stmt.target);
      if (stmt.target.kind == Expr::Kind::Index &&
          stmt.target.kids.size() >= 2) {
        const std::string obj = infer(stmt.target.kids[0]);
        const std::string elem = arrayElem(obj);
        const std::string rt = infer(stmt.expr);
        if (known(elem) && known(rt) && !compatible(elem, rt))
          fail(stmt.line, stmt.col, "cannot assign " + rt + " to " + elem);
        checkArrayElems(elem, stmt.expr, stmt.line, stmt.col);
      }
      return;
    }
    case Stmt::Kind::Return: {
      const bool prevThrowing = throwingTry;
      throwingTry = false;
      walkExpr(stmt.expr);
      if (throwingTry && !currentThrows)
        fail(stmt.line, stmt.col,
             "returning a throwing call requires 'throws'");
      throwingTry = prevThrowing || throwingTry;
      {
        std::string got = infer(stmt.expr);
        if (known(currentReturn) && known(got) &&
            !compatible(currentReturn, got))
          fail(stmt.line, stmt.col,
               "cannot return " + got + " from " + currentReturn +
                   " function");
        checkArrayElems(currentReturn, stmt.expr, stmt.line, stmt.col);
      }
      return;
    }
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
      const std::string it = infer(stmt.expr);
      if (isArrayTy(it))
        item = arrayElem(it);
      else if (isMapTy(it) || isString(it))
        item = "String";
      else if (it == "Int" || it == "Range")
        item = "Int";
      bind(stmt.name, item);
      for (const auto &s : stmt.body)
        walkStmt(s);
      scopes.pop_back();
      return;
    }
    case Stmt::Kind::Match: {
      walkExpr(stmt.expr);
      const std::string ty = infer(stmt.expr);
      const EnumDecl *en = known(ty) ? I().findEnum(ty) : nullptr;
      std::set<std::string> variants;
      if (en) {
        for (const auto &v : en->variants)
          variants.insert(v.name);
      }
      std::set<std::string> covered;
      bool wild = false;
      for (const auto &arm : stmt.arms) {
        if (arm.pat == MatchArm::Pat::Wildcard)
          wild = true;
        else if (arm.pat == MatchArm::Pat::Variant && en) {
          if (!variants.count(arm.name))
            fail(arm.line, arm.col,
                 "enum " + en->name + " has no variant '" + arm.name + "'");
          else
            covered.insert(arm.name);
        }
        scopes.emplace_back();
        for (const auto &b : arm.binds) {
          if (!b.empty())
            bind(b, "");
        }
        for (const auto &s : arm.body)
          walkStmt(s);
        scopes.pop_back();
      }
      if (en && !wild) {
        std::vector<std::string> missing;
        for (const auto &v : en->variants) {
          if (!covered.count(v.name))
            missing.push_back(v.name);
        }
        if (!missing.empty()) {
          std::string msg = "match of " + en->name + " is missing variant";
          if (missing.size() > 1)
            msg += "s";
          for (size_t i = 0; i < missing.size(); ++i) {
            msg += i == 0 ? " '" : ", '";
            msg += missing[i];
            msg += "'";
          }
          fail(stmt.line, stmt.col, msg);
        }
      }
      return;
    }
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
    auto prevParams = genericParams;
    auto prevBounds = genericBounds;
    auto pushParams = [&](const std::vector<TypeParam> &params) {
      for (const auto &p : params) {
        genericParams.insert(p.name);
        if (!p.bounds.empty())
          genericBounds[p.name] = p.bounds;
        for (const auto &b : p.bounds)
          checkTraitBound(b, fn.line, 1);
      }
    };
    pushParams(fn.typeParams);
    if (!selfType.empty()) {
      if (const structDecl *st = I().findStruct(selfType))
        pushParams(st->typeParams);
      for (const auto &im : I().traitImpls) {
        if (typeHead(im.typeName) == typeHead(selfType))
          pushParams(im.typeParams);
      }
      auto pit = I().classParents.find(typeHead(selfType));
      if (pit != I().classParents.end())
        currentSuper = pit->second;
    }
    checkTypeName(fn.returnType, fn.line, 1);
    scopes.emplace_back();
    for (size_t i = 0; i < fn.params.size(); ++i) {
      std::string ty = i < fn.paramTypes.size() ? fn.paramTypes[i] : "";
      if (fn.params[i] == "self" && !selfType.empty())
        ty = selfType;
      if (!ty.empty()) {
        checkTypeName(ty, fn.line, 1);
        if (const structDecl *st = I().findStruct(ty))
          checkTypeArgCount(st->typeParams, typeArgList(ty), fn.line, 1,
                            typeHead(ty));
        else if (const TraitDecl *tr = I().findTrait(ty))
          checkTypeArgCount(tr->typeParams, typeArgList(ty), fn.line, 1,
                            typeHead(ty));
      }
      bind(fn.params[i], ty);
    }
    for (const auto &stmt : fn.body)
      walkStmt(stmt);
    scopes.pop_back();
    genericParams = std::move(prevParams);
    genericBounds = std::move(prevBounds);
    currentReturn = prevRet;
    currentSelf = prevSelf;
    currentSuper = prevSuper;
    currentThrows = prevThrows;
  }

  void run() {
    for (const auto &st : I().program.structs) {
      auto prev = genericParams;
      for (const auto &p : st.typeParams)
        genericParams.insert(p.name);
      for (const auto &ty : st.fieldTypes)
        checkTypeName(ty, st.line, 1);
      genericParams = std::move(prev);
    }
    for (const auto &c : I().program.classes) {
      auto prev = genericParams;
      for (const auto &p : c.typeParams)
        genericParams.insert(p.name);
      for (const auto &f : c.fields)
        checkTypeName(f.type, c.line, 1);
      genericParams = std::move(prev);
    }
    for (const auto &fn : I().program.fns)
      checkFn(fn, "");
    for (const auto &type : I().typeMethods) {
      for (const auto &m : type.second) {
        const std::string prevMod = I().currentModule;
        I().currentModule = m.second->module;
        checkFn(*m.second, type.first);
        I().currentModule = prevMod;
      }
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
