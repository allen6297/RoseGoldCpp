#include "interp.h"
#include "parser.h"
#include "lexer.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
constexpr long long kMaxRangeItems = 1000000;
constexpr int kMaxJsonDepth = 64;
constexpr size_t kMaxRepeatBytes = 16u * 1024u * 1024u;
constexpr size_t kMaxCallDepth = 1024;
constexpr int kMaxSyncEmitDepth = 64;
constexpr long long kMaxSleepMs = 60000;
} // namespace

static Value zeroOfType(const std::string &ty) {
  if (ty == "Int")
    return Value::makeInt(0);
  if (ty == "Float")
    return Value::makeFloat(0);
  if (ty == "String" || ty == "Str")
    return Value::makeString("");
  if (ty == "Bool")
    return Value::makeBool(false);
  if (ty == "Array" ||
      (ty.size() > 6 && ty.compare(0, 6, "Array[") == 0 && ty.back() == ']'))
    return Value::makeArray({});
  if (ty == "Map" ||
      (ty.size() > 4 && ty.compare(0, 4, "Map[") == 0 && ty.back() == ']'))
    return Value::makeMap(std::make_shared<MapData>());
  return Value::makeVoid();
}

static bool sameFn(const Value &a, const Value &b) {
  if (a.kind != Value::Kind::FnRef || b.kind != Value::Kind::FnRef)
    return false;
  if (a.clo || b.clo)
    return a.clo.get() == b.clo.get();
  return a.s == b.s;
}

static bool skipCaptureName(const std::string &name) {
  return name == "print" || name == "len" || name == "assert" ||
         name == "argv" || name == "argv_len" || name == "super";
}

static void collectFreeExpr(const Expr &e, std::set<std::string> &bound,
                            std::set<std::string> &free);
static void collectFreeStmt(const Stmt &s, std::set<std::string> &bound,
                            std::set<std::string> &free);

static void collectFreeExpr(const Expr &e, std::set<std::string> &bound,
                            std::set<std::string> &free) {
  if (e.kind == Expr::Kind::Lambda && e.lambda) {
    auto inner = bound;
    for (const auto &p : e.lambda->params)
      inner.insert(p);
    for (const auto &st : e.lambda->body)
      collectFreeStmt(st, inner, free);
    return;
  }
  if (e.kind == Expr::Kind::Var) {
    if (!bound.count(e.text) && !skipCaptureName(e.text))
      free.insert(e.text);
    return;
  }
  // `refresh()` parses as Call with text, not Var — still a free name.
  if (e.kind == Expr::Kind::Call && e.module.empty() && !e.text.empty()) {
    if (!bound.count(e.text) && !skipCaptureName(e.text))
      free.insert(e.text);
  }
  for (const auto &kid : e.kids)
    collectFreeExpr(kid, bound, free);
}

static void collectFreeStmt(const Stmt &s, std::set<std::string> &bound,
                            std::set<std::string> &free) {
  switch (s.kind) {
  case Stmt::Kind::Var:
  case Stmt::Kind::Const:
    collectFreeExpr(s.expr, bound, free);
    bound.insert(s.name);
    return;
  case Stmt::Kind::Assign:
    collectFreeExpr(s.expr, bound, free);
    if (!bound.count(s.name) && !skipCaptureName(s.name))
      free.insert(s.name);
    return;
  case Stmt::Kind::FieldAssign:
  case Stmt::Kind::IndexAssign:
    collectFreeExpr(s.target, bound, free);
    collectFreeExpr(s.expr, bound, free);
    return;
  case Stmt::Kind::Return:
  case Stmt::Kind::Expr:
  case Stmt::Kind::Throw:
    collectFreeExpr(s.expr, bound, free);
    return;
  case Stmt::Kind::If:
    collectFreeExpr(s.expr, bound, free);
    for (const auto &st : s.body)
      collectFreeStmt(st, bound, free);
    for (const auto &st : s.elseBody)
      collectFreeStmt(st, bound, free);
    return;
  case Stmt::Kind::While:
    collectFreeExpr(s.expr, bound, free);
    for (const auto &st : s.body)
      collectFreeStmt(st, bound, free);
    return;
  case Stmt::Kind::For: {
    collectFreeExpr(s.expr, bound, free);
    auto inner = bound;
    inner.insert(s.name);
    for (const auto &st : s.body)
      collectFreeStmt(st, inner, free);
    return;
  }
  case Stmt::Kind::Match: {
    collectFreeExpr(s.expr, bound, free);
    for (const auto &arm : s.arms) {
      auto inner = bound;
      for (const auto &b : arm.binds)
        inner.insert(b);
      for (const auto &st : arm.body)
        collectFreeStmt(st, inner, free);
    }
    return;
  }
  case Stmt::Kind::Do: {
    for (const auto &st : s.body)
      collectFreeStmt(st, bound, free);
    auto inner = bound;
    if (!s.name.empty())
      inner.insert(s.name);
    for (const auto &st : s.elseBody)
      collectFreeStmt(st, inner, free);
    return;
  }
  default:
    return;
  }
}

const std::string *Interpreter::findModuleBind(const std::string &name) const {
  if (!currentModule.empty()) {
    auto lit = loaded.find(currentModule);
    if (lit == loaded.end())
      return nullptr;
    auto it = lit->second.modules.find(name);
    if (it != lit->second.modules.end())
      return &it->second;
    return nullptr;
  }
  auto it = moduleBinds.find(name);
  if (it != moduleBinds.end())
    return &it->second;
  return nullptr;
}

std::optional<std::string> Interpreter::modulePath(const Expr &e) {
  if (e.kind == Expr::Kind::Var) {
    if (const std::string *b = findModuleBind(e.text))
      return *b;
    return std::nullopt;
  }
  if (e.kind == Expr::Kind::Member) {
    std::optional<std::string> parent = modulePath(e.kids[0]);
    if (!parent)
      return std::nullopt;
    auto lit = loaded.find(*parent);
    if (lit == loaded.end())
      return std::nullopt;
    const bool internal = currentModule == *parent;
    const auto &map =
        internal ? lit->second.modules : lit->second.exportMods;
    auto it = map.find(e.text);
    if (it != map.end())
      return it->second;
    runtime("module '" + *parent + "' has no export '" + e.text + "'",
            e.line, e.col);
  }
  return std::nullopt;
}

const structDecl *Interpreter::findStruct(const std::string &name) const {
  const std::string head = typeHead(name);
  if (!currentModule.empty()) {
    auto lit = loaded.find(currentModule);
    if (lit != loaded.end()) {
      auto it = lit->second.structs.find(head);
      if (it != lit->second.structs.end())
        return it->second;
    }
  }
  auto it = structs.find(head);
  if (it != structs.end())
    return it->second;
  return nullptr;
}

const TraitDecl *Interpreter::findTrait(const std::string &name) const {
  auto it = traits.find(typeHead(name));
  if (it != traits.end())
    return it->second;
  return nullptr;
}

bool Interpreter::isDataType(const std::string &name) const {
  const std::string head = typeHead(name);
  auto it = allTypes.find(head);
  if (it != allTypes.end() && it->second)
    return it->second->isData;
  const structDecl *st = findStruct(head);
  return st && st->isData;
}

const EnumDecl *Interpreter::findEnum(const std::string &name) const {
  if (!currentModule.empty()) {
    auto lit = loaded.find(currentModule);
    if (lit != loaded.end()) {
      auto it = lit->second.enums.find(name);
      if (it != lit->second.enums.end())
        return it->second;
    }
  }
  auto it = enums.find(name);
  if (it != enums.end())
    return it->second;
  return nullptr;
}

FnDecl *Interpreter::findLocalFn(const std::string &name) {
  if (!currentModule.empty()) {
    auto lit = loaded.find(currentModule);
    if (lit != loaded.end()) {
      auto it = lit->second.fns.find(name);
      if (it != lit->second.fns.end())
        return it->second;
      auto fit = lit->second.fromFns.find(name);
      if (fit != lit->second.fromFns.end())
        return fit->second;
    }
    return nullptr;
  }
  auto it = fns.find(name);
  if (it != fns.end())
    return const_cast<FnDecl *>(it->second);
  return nullptr;
}

FnDecl *Interpreter::findUfcs(const std::string &name,
                              const std::string &recvTy) {
  std::vector<FnDecl *> candidates;
  auto addCand = [&](FnDecl *fn) {
    if (!fn || !fn->isUfcs)
      return;
    for (FnDecl *c : candidates) {
      if (c == fn)
        return;
    }
    candidates.push_back(fn);
  };
  auto addFromMod = [&](LoadedMod &m) {
    auto uit = m.ufcsFns.find(name);
    if (uit != m.ufcsFns.end()) {
      for (FnDecl *fn : uit->second)
        addCand(fn);
      return;
    }
    auto eit = m.exports.find(name);
    if (eit != m.exports.end())
      addCand(eit->second);
    auto fit = m.fns.find(name);
    if (fit != m.fns.end())
      addCand(fit->second);
  };

  if (!currentModule.empty()) {
    auto lit = loaded.find(currentModule);
    if (lit != loaded.end()) {
      addFromMod(lit->second);
      auto fit = lit->second.fromFns.find(name);
      if (fit != lit->second.fromFns.end())
        addCand(fit->second);
    }
  } else {
    FnDecl *fn = findLocalFn(name);
    addCand(fn);
  }

  std::vector<std::string> queue;
  std::set<std::string> seen;
  auto enqueue = [&](const std::map<std::string, std::string> &binds) {
    for (const auto &kv : binds)
      queue.push_back(kv.second);
  };
  if (!currentModule.empty()) {
    auto lit = loaded.find(currentModule);
    if (lit != loaded.end())
      enqueue(lit->second.modules);
  } else {
    enqueue(moduleBinds);
  }
  for (size_t i = 0; i < queue.size(); ++i) {
    const std::string modName = queue[i];
    if (!seen.insert(modName).second)
      continue;
    auto lit = loaded.find(modName);
    if (lit == loaded.end())
      continue;
    addFromMod(lit->second);
    for (const auto &kv : lit->second.exportMods)
      queue.push_back(kv.second);
    for (const auto &kv : lit->second.modules)
      queue.push_back(kv.second);
  }

  if (candidates.empty())
    return nullptr;
  if (recvTy.empty() || candidates.size() == 1)
    return candidates[0];

  const std::string want = typeHead(recvTy);
  FnDecl *exact = nullptr;
  FnDecl *fallback = nullptr;
  for (FnDecl *fn : candidates) {
    if (fn->paramTypes.empty() || fn->paramTypes[0].empty()) {
      if (!fallback)
        fallback = fn;
      continue;
    }
    if (typeHead(fn->paramTypes[0]) == want) {
      exact = fn;
      break;
    }
  }
  if (exact)
    return exact;
  return fallback ? fallback : candidates[0];
}

Value Interpreter::callUfcs(const FnDecl &fn, const Value &obj,
                            const std::vector<Value> &args, int line, int col) {
  std::vector<Value> all;
  all.reserve(args.size() + 1);
  all.push_back(obj);
  all.insert(all.end(), args.begin(), args.end());
  return callUser(fn, all, line, col);
}

Value Interpreter::callModule(const std::string &modName, const std::string &name,
                 const std::vector<Value> &args, int line, int col) {
  auto it = loaded.find(modName);
  if (it == loaded.end())
    runtime("unknown function " + modName + "." + name, line, col);
  auto eit = it->second.exports.find(name);
  if (eit == it->second.exports.end())
    runtime("module '" + modName + "' has no export '" + name + "'", line,
            col);
  return callUser(*eit->second, args, line, col);
}

Binding *Interpreter::findVar(const std::string &name) {
  for (int i = static_cast<int>(env.size()) - 1; i >= 0; --i) {
    auto it = env[static_cast<size_t>(i)].find(name);
    if (it != env[static_cast<size_t>(i)].end())
      return &it->second;
  }
  return nullptr;
}

Binding *Interpreter::findLocalBinding(const std::string &name) {
  if (env.size() < 2)
    return nullptr;
  auto it = env.back().find(name);
  if (it != env.back().end())
    return &it->second;
  return nullptr;
}

Binding *Interpreter::findGlobalBinding(const std::string &name) {
  if (env.empty())
    return nullptr;
  auto it = env[0].find(name);
  if (it != env[0].end())
    return &it->second;
  return nullptr;
}

StructData *Interpreter::selfRec() {
  Binding *self = findLocalBinding("self");
  if (!self || self->value.kind != Value::Kind::Struct || !self->value.rec)
    return nullptr;
  return self->value.rec.get();
}

Value *Interpreter::fieldOnSelf(const std::string &name) {
  StructData *rec = selfRec();
  if (!rec)
    return nullptr;
  auto it = rec->fields.find(name);
  if (it == rec->fields.end())
    return nullptr;
  return &it->second;
}

Value Interpreter::resolveValue(const std::string &name, int line, int col) {
  if (Binding *local = findLocalBinding(name))
    return local->value;
  if (Value *field = fieldOnSelf(name))
    return *field;
  if (Binding *self = findLocalBinding("self")) {
    if (self->value.kind == Value::Kind::Struct && self->value.rec &&
        lookupTypeSignal(self->value.rec->name, name))
      return Value::makeSignalRef(name, self->value.rec);
  }
  if (Binding *global = findGlobalBinding(name))
    return global->value;
  if (findLocalFn(name))
    return Value::makeFnRef(name);
  if (const EnumDecl *en = findEnum(name))
    return Value::makeEnumType(en->name);
  runtime("undefined variable '" + name + "'", line, col);
}

Value Interpreter::constructEnum(const EnumDecl &en, const std::string &variant,
                    std::vector<Value> args, int line, int col) {
  const EnumVariant *found = nullptr;
  for (const auto &v : en.variants) {
    if (v.name == variant) {
      found = &v;
      break;
    }
  }
  if (!found)
    runtime("enum " + en.name + " has no variant '" + variant + "'", line,
            col);
  if (static_cast<int>(args.size()) != found->arity)
    runtime(en.name + "." + variant + " takes " +
                std::to_string(found->arity) + " argument(s)",
            line, col);
  return Value::makeEnum(en.name, variant, std::move(args));
}

Value Interpreter::selfObject(int line, int col) {
  Binding *self = findVar("self");
  if (!self || self->value.kind != Value::Kind::Struct || !self->value.rec)
    runtime("super requires self", line, col);
  return self->value;
}

Value Interpreter::callSuper(const std::string &name, const std::vector<Value> &args,
                int line, int col) {
  if (superType.empty())
    runtime(
        "super is only valid in a method of a class that extends another",
        line, col);
  return invokeTypeMethod(selfObject(line, col), superType, name, args, line,
                          col);
}

size_t Interpreter::indexAt(const Value &idx, int line, int col) {
  if (idx.kind != Value::Kind::Int)
    runtime("index must be Int", line, col);
  if (idx.i < 0)
    runtime("index " + std::to_string(idx.i) + " out of bounds", line, col);
  return static_cast<size_t>(idx.i);
}

Value Interpreter::indexGet(const Value &obj, const Value &idx, int line, int col) {
  if (obj.kind == Value::Kind::Map) {
    if (idx.kind != Value::Kind::String)
      runtime("map key must be String", line, col);
    if (!obj.dict)
      runtime("key '" + idx.s + "' not found", line, col);
    auto it = obj.dict->fields.find(idx.s);
    if (it == obj.dict->fields.end())
      runtime("key '" + idx.s + "' not found", line, col);
    return it->second;
  }
  const size_t i = indexAt(idx, line, col);
  if (obj.kind == Value::Kind::Array) {
    if (!obj.items || i >= obj.items->size())
      runtime("index " + std::to_string(i) + " out of bounds", line, col);
    return (*obj.items)[i];
  }
  if (obj.kind == Value::Kind::String) {
    if (i >= obj.s.size())
      runtime("index " + std::to_string(i) + " out of bounds", line, col);
    return Value::makeString(std::string(1, obj.s[i]));
  }
  runtime("cannot index " + obj.toString(), line, col);
}

void Interpreter::indexSet(const Value &obj, const Value &idx, Value value, int line,
              int col) {
  if (obj.kind == Value::Kind::Map) {
    if (!obj.dict)
      runtime("cannot assign to " + obj.toString(), line, col);
    if (idx.kind != Value::Kind::String)
      runtime("map key must be String", line, col);
    if (!obj.dict->fields.count(idx.s))
      obj.dict->order.push_back(idx.s);
    obj.dict->fields[idx.s] = std::move(value);
    return;
  }
  if (obj.kind != Value::Kind::Array || !obj.items)
    runtime("cannot assign to " + obj.toString(), line, col);
  const size_t i = indexAt(idx, line, col);
  if (i >= obj.items->size())
    runtime("index " + std::to_string(i) + " out of bounds", line, col);
  (*obj.items)[i] = std::move(value);
}

std::string Interpreter::mapKey(const Value &v, int line, int col) {
  if (v.kind == Value::Kind::String)
    return v.s;
  if (v.kind == Value::Kind::Int)
    return std::to_string(v.i);
  if (v.kind == Value::Kind::Bool)
    return v.b ? "true" : "false";
  runtime("map key must be String", line, col);
}

Value Interpreter::arrayLen(const Value &obj) {
  if (!obj.items)
    return Value::makeInt(0);
  return Value::makeInt(static_cast<long long>(obj.items->size()));
}

Value Interpreter::mapLen(const Value &obj) {
  if (!obj.dict)
    return Value::makeInt(0);
  return Value::makeInt(static_cast<long long>(obj.dict->fields.size()));
}

std::vector<Value> Interpreter::iterItems(const Value &iter, int line, int col) {
  if (iter.kind == Value::Kind::Array) {
    if (!iter.items)
      return {};
    return *iter.items;
  }
  if (iter.kind == Value::Kind::Map) {
    std::vector<Value> keys;
    if (!iter.dict)
      return keys;
    keys.reserve(iter.dict->order.size());
    for (const auto &k : iter.dict->order)
      keys.push_back(Value::makeString(k));
    return keys;
  }
  if (iter.kind == Value::Kind::String) {
    std::vector<Value> chars;
    chars.reserve(iter.s.size());
    for (char c : iter.s)
      chars.push_back(Value::makeString(std::string(1, c)));
    return chars;
  }
  if (iter.kind == Value::Kind::Int) {
    if (iter.i <= 0)
      return {};
    if (iter.i > kMaxRangeItems)
      runtime("range too large", line, col);
    std::vector<Value> nums;
    nums.reserve(static_cast<size_t>(iter.i));
    for (long long n = 0; n < iter.i; ++n)
      nums.push_back(Value::makeInt(n));
    return nums;
  }
  if (iter.kind == Value::Kind::Range) {
    long long start = iter.i;
    long long end = iter.payload.empty() ? 0 : iter.payload[0].i;
    std::vector<Value> nums;
    if (iter.b) {
      for (long long n = start;; ++n) {
        if (n > end)
          break;
        if (static_cast<long long>(nums.size()) >= kMaxRangeItems)
          runtime("range too large", line, col);
        nums.push_back(Value::makeInt(n));
        if (n == std::numeric_limits<long long>::max())
          break;
      }
    } else {
      for (long long n = start; n < end; ++n) {
        if (static_cast<long long>(nums.size()) >= kMaxRangeItems)
          runtime("range too large", line, col);
        nums.push_back(Value::makeInt(n));
        if (n == std::numeric_limits<long long>::max())
          break;
      }
    }
    return nums;
  }
  runtime("cannot iterate over " + iter.toString(), line, col);
}

Value Interpreter::callValueMethod(const Value &obj, const std::string &name,
                      const std::vector<Value> &args, int line, int col) {
  if (obj.kind == Value::Kind::Array) {
    if (name == "len") {
      if (!args.empty())
        runtime("Array.len takes 0 arguments", line, col);
      return arrayLen(obj);
    }
    if (name == "push") {
      if (args.size() != 1)
        runtime("Array.push takes 1 argument", line, col);
      if (!obj.items)
        runtime("cannot push on array", line, col);
      obj.items->push_back(args[0]);
      return Value::makeVoid();
    }
    if (name == "pop") {
      if (!args.empty())
        runtime("Array.pop takes 0 arguments", line, col);
      if (!obj.items || obj.items->empty())
        runtime("pop from empty array", line, col);
      Value v = obj.items->back();
      obj.items->pop_back();
      return v;
    }
    if (FnDecl *fn = findUfcs(name, "Array"))
      return callUfcs(*fn, obj, args, line, col);
    runtime("Array has no method '" + name + "'", line, col);
  }
  if (obj.kind == Value::Kind::Map) {
    if (!obj.dict)
      runtime("cannot call method '" + name + "' on " + obj.toString(), line,
              col);
    if (name == "len") {
      if (!args.empty())
        runtime("Map.len takes 0 arguments", line, col);
      return mapLen(obj);
    }
    if (name == "has") {
      if (args.size() != 1)
        runtime("Map.has takes 1 argument", line, col);
      return Value::makeBool(
          obj.dict->fields.count(mapKey(args[0], line, col)) != 0);
    }
    if (name == "keys") {
      if (!args.empty())
        runtime("Map.keys takes 0 arguments", line, col);
      std::vector<Value> keys;
      keys.reserve(obj.dict->order.size());
      for (const auto &k : obj.dict->order)
        keys.push_back(Value::makeString(k));
      return Value::makeArray(std::move(keys));
    }
    if (name == "remove") {
      if (args.size() != 1)
        runtime("Map.remove takes 1 argument", line, col);
      const std::string key = mapKey(args[0], line, col);
      auto it = obj.dict->fields.find(key);
      if (it == obj.dict->fields.end())
        runtime("key '" + key + "' not found", line, col);
      Value v = it->second;
      obj.dict->fields.erase(it);
      obj.dict->order.erase(std::remove(obj.dict->order.begin(),
                                        obj.dict->order.end(), key),
                            obj.dict->order.end());
      return v;
    }
    if (name == "insert") {
      if (args.size() != 2)
        runtime("Map.insert takes 2 arguments", line, col);
      const std::string key = mapKey(args[0], line, col);
      if (!obj.dict->fields.count(key))
        obj.dict->order.push_back(key);
      obj.dict->fields[key] = args[1];
      return Value::makeVoid();
    }
    if (FnDecl *fn = findUfcs(name, "Map"))
      return callUfcs(*fn, obj, args, line, col);
    runtime("Map has no method '" + name + "'", line, col);
  }
  if (obj.kind == Value::Kind::String) {
    if (name == "len") {
      if (!args.empty())
        runtime("String.len takes 0 arguments", line, col);
      return Value::makeInt(static_cast<long long>(obj.s.size()));
    }
    if (FnDecl *fn = findUfcs(name, "String"))
      return callUfcs(*fn, obj, args, line, col);
    runtime("String has no method '" + name + "'", line, col);
  }
  if (obj.kind == Value::Kind::Future) {
    if (name == "cancel") {
      if (!args.empty())
        runtime("Future.cancel takes 0 arguments", line, col);
      cancelFuture(obj.fut, line, col);
      return Value::makeVoid();
    }
    runtime("Future has no method '" + name + "'", line, col);
  }
  if (FnDecl *fn = findUfcs(name))
    return callUfcs(*fn, obj, args, line, col);
  runtime("cannot call method '" + name + "' on " + obj.toString(), line,
          col);
}

Value Interpreter::readValueMember(const Value &obj, const std::string &name, int line,
                      int col) {
  if (obj.kind == Value::Kind::Array) {
    if (name == "len")
      return arrayLen(obj);
    runtime("Array has no member '" + name + "'", line, col);
  }
  if (obj.kind == Value::Kind::Map) {
    if (name == "len")
      return mapLen(obj);
    runtime("Map has no member '" + name + "'", line, col);
  }
  if (obj.kind == Value::Kind::String) {
    if (name == "len")
      return Value::makeInt(static_cast<long long>(obj.s.size()));
    runtime("String has no member '" + name + "'", line, col);
  }
  runtime("cannot read field on " + obj.toString(), line, col);
}

Value Interpreter::applyBinop(const std::string &op, const Value &a, const Value &b,
                              int line, int col) {
  if (op == "+" && a.kind == Value::Kind::String &&
      b.kind == Value::Kind::String)
    return Value::makeString(a.s + b.s);
  if (op == "==")
    return Value::makeBool(a.equals(b));
  if (op == "!=")
    return Value::makeBool(!a.equals(b));
  if (!isNumeric(a) || !isNumeric(b))
    runtime("operands must be numbers", line, col);
  const bool bothInt =
      a.kind == Value::Kind::Int && b.kind == Value::Kind::Int;
  if (op == "+") {
    if (bothInt) {
      long long r = 0;
      if (__builtin_add_overflow(a.i, b.i, &r))
        runtime("integer overflow", line, col);
      return Value::makeInt(r);
    }
    return Value::makeFloat(asF64(a) + asF64(b));
  }
  if (op == "-") {
    if (bothInt) {
      long long r = 0;
      if (__builtin_sub_overflow(a.i, b.i, &r))
        runtime("integer overflow", line, col);
      return Value::makeInt(r);
    }
    return Value::makeFloat(asF64(a) - asF64(b));
  }
  if (op == "*") {
    if (bothInt) {
      long long r = 0;
      if (__builtin_mul_overflow(a.i, b.i, &r))
        runtime("integer overflow", line, col);
      return Value::makeInt(r);
    }
    return Value::makeFloat(asF64(a) * asF64(b));
  }
  if (op == "/") {
    if (bothInt) {
      if (b.i == 0)
        runtime("division by zero", line, col);
      if (a.i == std::numeric_limits<long long>::min() && b.i == -1)
        runtime("integer overflow", line, col);
      return Value::makeInt(a.i / b.i);
    }
    if (asF64(b) == 0.0)
      runtime("division by zero", line, col);
    return Value::makeFloat(asF64(a) / asF64(b));
  }
  if (op == "%") {
    if (bothInt) {
      if (b.i == 0)
        runtime("modulo by zero", line, col);
      if (a.i == std::numeric_limits<long long>::min() && b.i == -1)
        runtime("integer overflow", line, col);
      return Value::makeInt(a.i % b.i);
    }
    if (asF64(b) == 0.0)
      runtime("modulo by zero", line, col);
    return Value::makeFloat(std::fmod(asF64(a), asF64(b)));
  }
  if (op == "<") {
    if (bothInt)
      return Value::makeBool(a.i < b.i);
    return Value::makeBool(asF64(a) < asF64(b));
  }
  if (op == ">") {
    if (bothInt)
      return Value::makeBool(a.i > b.i);
    return Value::makeBool(asF64(a) > asF64(b));
  }
  if (op == "<=") {
    if (bothInt)
      return Value::makeBool(a.i <= b.i);
    return Value::makeBool(asF64(a) <= asF64(b));
  }
  if (op == ">=") {
    if (bothInt)
      return Value::makeBool(a.i >= b.i);
    return Value::makeBool(asF64(a) >= asF64(b));
  }
  runtime("unknown operator", line, col);
}

Value Interpreter::applyAssignOp(const std::string &op, const Value &old,
                                 const Value &rhs, int line, int col) {
  if (op.empty() || op == "=")
    return rhs;
  if (op.size() < 2 || op.back() != '=')
    runtime("unknown operator", line, col);
  return applyBinop(op.substr(0, op.size() - 1), old, rhs, line, col);
}

Value Interpreter::eval(const Expr &e) {
  switch (e.kind) {
  case Expr::Kind::Int:
    return Value::makeInt(e.number);
  case Expr::Kind::Float:
    return Value::makeFloat(e.real);
  case Expr::Kind::String:
    return Value::makeString(e.text);
  case Expr::Kind::Bool:
    return Value::makeBool(e.boolean);
  case Expr::Kind::Var: {
    if (e.text == "super")
      runtime(
          "super is only valid in a method of a class that extends another",
          e.line, e.col);
    return resolveValue(e.text, e.line, e.col);
  }
  case Expr::Kind::Unary: {
    Value v = eval(e.kids[0]);
    if (e.text == "-") {
      if (v.kind == Value::Kind::Float)
        return Value::makeFloat(-v.real);
      if (v.kind != Value::Kind::Int)
        runtime("unary '-' expects a number", e.line, e.col);
      if (v.i == std::numeric_limits<long long>::min())
        runtime("integer overflow", e.line, e.col);
      return Value::makeInt(-v.i);
    }
    return Value::makeBool(!v.truthy());
  }
  case Expr::Kind::Binary: {
    if (e.text == "&&") {
      Value a = eval(e.kids[0]);
      if (!a.truthy())
        return Value::makeBool(false);
      return Value::makeBool(eval(e.kids[1]).truthy());
    }
    if (e.text == "||") {
      Value a = eval(e.kids[0]);
      if (a.truthy())
        return Value::makeBool(true);
      return Value::makeBool(eval(e.kids[1]).truthy());
    }
    return applyBinop(e.text, eval(e.kids[0]), eval(e.kids[1]), e.line, e.col);
  }
  case Expr::Kind::Try:
    return eval(e.kids[0]);
  case Expr::Kind::Await: {
    if (e.kids.empty())
      runtime("invalid await", e.line, e.col);
    Value f = eval(e.kids[0]);
    if (f.kind != Value::Kind::Future || !f.fut)
      runtime("can only await a Future", e.line, e.col);
    return awaitFuture(f.fut, e.line, e.col);
  }
  case Expr::Kind::Lambda:
    return evalLambda(e);
  case Expr::Kind::Call: {
    std::vector<Value> args;
    if (e.text.empty()) {
      if (e.kids.empty())
        runtime("can only call a function", e.line, e.col);
      Value fn = eval(e.kids[0]);
      args.reserve(e.kids.size() > 0 ? e.kids.size() - 1 : 0);
      for (size_t i = 1; i < e.kids.size(); ++i)
        args.push_back(eval(e.kids[i]));
      return callFnValue(fn, args, e.line, e.col);
    }
    args.reserve(e.kids.size());
    for (const auto &kid : e.kids)
      args.push_back(eval(kid));
    return call(e.module, e.text, args, e.line, e.col);
  }
  case Expr::Kind::MethodCall: {
    std::vector<Value> args;
    args.reserve(e.kids.size() > 0 ? e.kids.size() - 1 : 0);
    for (size_t i = 1; i < e.kids.size(); ++i)
      args.push_back(eval(e.kids[i]));
    const Expr &recv = e.kids[0];
    if (recv.kind == Expr::Kind::Var && recv.text == "super")
      return callSuper(e.text, args, e.line, e.col);
    if (recv.kind == Expr::Kind::Var) {
      if (Binding *self = findLocalBinding("self")) {
        if (self->value.kind == Value::Kind::Struct && self->value.rec &&
            lookupTypeSignal(self->value.rec->name, recv.text))
          return dispatchSignal(Value::makeSignalRef(recv.text, self->value.rec),
                                e.text, args, e.line, e.col);
      }
      if (signalArity.count(recv.text))
        return callSignal(recv.text, e.text, args, e.line, e.col);
      if (const std::string *modName = findModuleBind(recv.text))
        return callModule(*modName, e.text, args, e.line, e.col);
      if (findLocalBinding(recv.text) || fieldOnSelf(recv.text) ||
          findGlobalBinding(recv.text) || findEnum(recv.text)) {
        Value obj = resolveValue(recv.text, recv.line, recv.col);
        if (obj.kind == Value::Kind::SignalRef)
          return dispatchSignal(obj, e.text, args, e.line, e.col);
        if (obj.kind == Value::Kind::Struct && obj.rec)
          return callTypeMethod(obj, e.text, args, e.line, e.col);
        if (obj.kind == Value::Kind::EnumType) {
          const EnumDecl *en = findEnum(obj.s);
          if (!en)
            en = findEnum(recv.text);
          if (!en)
            runtime("undefined enum '" + obj.s + "'", e.line, e.col);
          return constructEnum(*en, e.text, std::move(args), e.line, e.col);
        }
        if (obj.kind == Value::Kind::Array || obj.kind == Value::Kind::String ||
            obj.kind == Value::Kind::Map || obj.kind == Value::Kind::Future)
          return callValueMethod(obj, e.text, args, e.line, e.col);
        if (FnDecl *fn = findUfcs(e.text))
          return callUfcs(*fn, obj, args, e.line, e.col);
        runtime("cannot call method '" + e.text + "' on " + obj.toString(),
                e.line, e.col);
      }
      return callBuiltin(recv.text, e.text, args, e.line, e.col);
    }
    if (std::optional<std::string> modName = modulePath(recv))
      return callModule(*modName, e.text, args, e.line, e.col);
    Value obj = eval(recv);
    if (obj.kind == Value::Kind::SignalRef)
      return dispatchSignal(obj, e.text, args, e.line, e.col);
    if (obj.kind == Value::Kind::Struct && obj.rec)
      return callTypeMethod(obj, e.text, args, e.line, e.col);
    if (obj.kind == Value::Kind::EnumType) {
      const EnumDecl *en = findEnum(obj.s);
      if (!en)
        runtime("undefined enum '" + obj.s + "'", e.line, e.col);
      return constructEnum(*en, e.text, std::move(args), e.line, e.col);
    }
    if (obj.kind == Value::Kind::Array || obj.kind == Value::Kind::String ||
        obj.kind == Value::Kind::Map || obj.kind == Value::Kind::Future)
      return callValueMethod(obj, e.text, args, e.line, e.col);
    if (FnDecl *fn = findUfcs(e.text))
      return callUfcs(*fn, obj, args, e.line, e.col);
    runtime("cannot call method '" + e.text + "' on " + obj.toString(),
            e.line, e.col);
  }
  case Expr::Kind::Member: {
    Value obj;
    if (e.kids[0].kind == Expr::Kind::Var && e.kids[0].text == "super")
      obj = selfObject(e.line, e.col);
    else
      obj = eval(e.kids[0]);
    if (obj.kind == Value::Kind::EnumType) {
      const EnumDecl *en = findEnum(obj.s);
      if (!en)
        runtime("undefined enum '" + obj.s + "'", e.line, e.col);
      return constructEnum(*en, e.text, {}, e.line, e.col);
    }
    if (obj.kind == Value::Kind::Array || obj.kind == Value::Kind::String ||
        obj.kind == Value::Kind::Map)
      return readValueMember(obj, e.text, e.line, e.col);
    if (obj.kind != Value::Kind::Struct || !obj.rec)
      runtime("cannot read field on " + obj.toString(), e.line, e.col);
    auto it = obj.rec->fields.find(e.text);
    if (it == obj.rec->fields.end()) {
      if (lookupTypeSignal(obj.rec->name, e.text))
        return Value::makeSignalRef(e.text, obj.rec);
      runtime("struct " + obj.rec->name + " has no field '" + e.text + "'",
              e.line, e.col);
    }
    return it->second;
  }
  case Expr::Kind::StructLit: {
    auto absIt = classAbstract.find(e.text);
    if (absIt != classAbstract.end() && absIt->second)
      runtime("cannot construct abstract class '" + e.text + "'", e.line,
              e.col);
    const structDecl *st = findStruct(e.text);
    if (!st)
      runtime("undefined struct '" + e.text + "'", e.line, e.col);
    const structDecl &decl = *st;
    auto data = std::make_shared<StructData>();
    data->name = decl.name;
    data->order = decl.fields;
    for (size_t i = 0; i < e.names.size(); ++i) {
      const std::string &field = e.names[i];
      if (std::find(decl.fields.begin(), decl.fields.end(), field) ==
          decl.fields.end())
        runtime("unknown field '" + field + "' on " + decl.name, e.line,
                e.col);
      if (data->fields.count(field))
        runtime("duplicate field '" + field + "' on " + decl.name, e.line,
                e.col);
      data->fields[field] = eval(e.kids[i]);
    }
    auto defIt = fieldDefaults.find(decl.name);
    for (const auto &field : decl.fields) {
      if (data->fields.count(field))
        continue;
      if (defIt != fieldDefaults.end()) {
        auto eit = defIt->second.find(field);
        if (eit != defIt->second.end()) {
          data->fields[field] = eval(*eit->second);
          continue;
        }
      }
      if (decl.isOptionalField(field)) {
        data->fields[field] = zeroOfType(decl.typeOfField(field));
        continue;
      }
      runtime("missing field '" + field + "' on " + decl.name, e.line, e.col);
    }
    initInstanceSignals(*data);
    return Value::makeStruct(std::move(data));
  }
  case Expr::Kind::Array: {
    std::vector<Value> elems;
    elems.reserve(e.kids.size());
    for (const auto &kid : e.kids)
      elems.push_back(eval(kid));
    return Value::makeArray(std::move(elems));
  }
  case Expr::Kind::Range: {
    Value a = eval(e.kids[0]);
    Value b = eval(e.kids[1]);
    if (a.kind != Value::Kind::Int)
      runtime("range start must be Int", e.kids[0].line, e.kids[0].col);
    if (b.kind != Value::Kind::Int)
      runtime("range end must be Int", e.kids[1].line, e.kids[1].col);
    return Value::makeRange(a.i, b.i, e.boolean);
  }
  case Expr::Kind::Index:
    return indexGet(eval(e.kids[0]), eval(e.kids[1]), e.line, e.col);
  case Expr::Kind::Map: {
    auto data = std::make_shared<MapData>();
    if (e.kids.size() % 2 != 0)
      runtime("invalid map literal", e.line, e.col);
    for (size_t i = 0; i < e.kids.size(); i += 2) {
      Value key = eval(e.kids[i]);
      if (key.kind != Value::Kind::String)
        runtime("map key must be String", e.kids[i].line, e.kids[i].col);
      if (!data->fields.count(key.s))
        data->order.push_back(key.s);
      data->fields[key.s] = eval(e.kids[i + 1]);
    }
    return Value::makeMap(std::move(data));
  }
  }
  return Value::makeVoid();
}

namespace {

std::string jsonEscape(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 8);
  const char *hex = "0123456789abcdef";
  for (unsigned char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (c < 0x20) {
        out += "\\u00";
        out += hex[c >> 4];
        out += hex[c & 0xf];
      } else {
        out += static_cast<char>(c);
      }
    }
  }
  return out;
}

void jsonUtf8(std::string &out, unsigned cp) {
  if (cp <= 0x7F) {
    out += static_cast<char>(cp);
  } else if (cp <= 0x7FF) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp <= 0xFFFF) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

struct JsonParser {
  const std::string &src;
  size_t i = 0;
  int errLine;
  int errCol;

  explicit JsonParser(const std::string &s, int line, int col)
      : src(s), errLine(line), errCol(col) {}

  [[noreturn]] void fail(const std::string &msg) const {
    throw ThrowEscape{Value::makeString(msg), errLine, errCol};
  }

  char peek() const { return i < src.size() ? src[i] : '\0'; }

  char getc() {
    if (i >= src.size())
      fail("invalid JSON");
    return src[i++];
  }

  void skip() {
    while (i < src.size() &&
           std::isspace(static_cast<unsigned char>(src[i])))
      ++i;
  }

  bool matchLit(const char *lit) {
    size_t n = 0;
    while (lit[n])
      ++n;
    if (src.compare(i, n, lit) != 0)
      return false;
    i += n;
    return true;
  }

  unsigned hex4() {
    unsigned n = 0;
    for (int k = 0; k < 4; ++k) {
      unsigned char c = static_cast<unsigned char>(getc());
      n <<= 4;
      if (c >= '0' && c <= '9')
        n += static_cast<unsigned>(c - '0');
      else if (c >= 'a' && c <= 'f')
        n += static_cast<unsigned>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F')
        n += static_cast<unsigned>(c - 'A' + 10);
      else
        fail("invalid JSON");
    }
    return n;
  }

  std::string parseString() {
    if (getc() != '"')
      fail("invalid JSON");
    std::string out;
    while (true) {
      char c = getc();
      if (c == '"')
        return out;
      if (c == '\\') {
        char e = getc();
        switch (e) {
        case '"':
        case '\\':
        case '/':
          out += e;
          break;
        case 'b':
          out += '\b';
          break;
        case 'f':
          out += '\f';
          break;
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case 't':
          out += '\t';
          break;
        case 'u':
          jsonUtf8(out, hex4());
          break;
        default:
          fail("invalid JSON");
        }
      } else if (static_cast<unsigned char>(c) < 0x20) {
        fail("invalid JSON");
      } else {
        out += c;
      }
    }
  }

  Value parseNumber() {
    const size_t start = i;
    if (peek() == '-')
      ++i;
    if (peek() == '0') {
      ++i;
      if (std::isdigit(static_cast<unsigned char>(peek())))
        fail("invalid JSON");
    } else if (std::isdigit(static_cast<unsigned char>(peek()))) {
      while (std::isdigit(static_cast<unsigned char>(peek())))
        ++i;
    } else {
      fail("invalid JSON");
    }
    bool frac = false;
    if (peek() == '.') {
      frac = true;
      ++i;
      if (!std::isdigit(static_cast<unsigned char>(peek())))
        fail("invalid JSON");
      while (std::isdigit(static_cast<unsigned char>(peek())))
        ++i;
    }
    if (peek() == 'e' || peek() == 'E') {
      frac = true;
      ++i;
      if (peek() == '+' || peek() == '-')
        ++i;
      if (!std::isdigit(static_cast<unsigned char>(peek())))
        fail("invalid JSON");
      while (std::isdigit(static_cast<unsigned char>(peek())))
        ++i;
    }
    const std::string tok = src.substr(start, i - start);
    if (!frac) {
      try {
        size_t idx = 0;
        long long n = std::stoll(tok, &idx, 10);
        if (idx == tok.size())
          return Value::makeInt(n);
      } catch (...) {
      }
    }
    try {
      size_t idx = 0;
      double n = std::stod(tok, &idx);
      if (idx != tok.size() || !std::isfinite(n))
        fail("invalid JSON");
      return Value::makeFloat(n);
    } catch (...) {
      fail("invalid JSON");
    }
    return Value::makeFloat(0);
  }

  Value parseValue(int depth = 0) {
    if (depth > kMaxJsonDepth)
      fail("JSON nesting too deep");
    skip();
    char c = peek();
    if (c == '"')
      return Value::makeString(parseString());
    if (c == '{') {
      getc();
      auto data = std::make_shared<MapData>();
      skip();
      if (peek() == '}') {
        getc();
        return Value::makeMap(std::move(data));
      }
      while (true) {
        skip();
        if (peek() != '"')
          fail("invalid JSON");
        std::string key = parseString();
        skip();
        if (getc() != ':')
          fail("invalid JSON");
        Value val = parseValue(depth + 1);
        if (!data->fields.count(key))
          data->order.push_back(key);
        data->fields[key] = std::move(val);
        skip();
        char sep = getc();
        if (sep == '}')
          return Value::makeMap(std::move(data));
        if (sep != ',')
          fail("invalid JSON");
      }
    }
    if (c == '[') {
      getc();
      std::vector<Value> items;
      skip();
      if (peek() == ']') {
        getc();
        return Value::makeArray(std::move(items));
      }
      while (true) {
        items.push_back(parseValue(depth + 1));
        skip();
        char sep = getc();
        if (sep == ']')
          return Value::makeArray(std::move(items));
        if (sep != ',')
          fail("invalid JSON");
      }
    }
    if (c == 't') {
      if (!matchLit("true"))
        fail("invalid JSON");
      return Value::makeBool(true);
    }
    if (c == 'f') {
      if (!matchLit("false"))
        fail("invalid JSON");
      return Value::makeBool(false);
    }
    if (c == 'n') {
      if (!matchLit("null"))
        fail("invalid JSON");
      fail("json null is not supported");
    }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
      return parseNumber();
    fail("invalid JSON");
  }

  Value parse() {
    Value v = parseValue();
    skip();
    if (i != src.size())
      fail("invalid JSON");
    return v;
  }
};

bool jsonStringify(const Value &v, std::string &out, int depth,
                   std::string &err) {
  if (depth > 64) {
    err = "json nesting too deep";
    return false;
  }
  switch (v.kind) {
  case Value::Kind::Bool:
    out += v.b ? "true" : "false";
    return true;
  case Value::Kind::Int:
    out += std::to_string(v.i);
    return true;
  case Value::Kind::Float: {
    if (!std::isfinite(v.real)) {
      err = "cannot stringify non-finite Float";
      return false;
    }
    std::ostringstream ss;
    ss << std::setprecision(17) << v.real;
    out += ss.str();
    return true;
  }
  case Value::Kind::String:
    out += '"';
    out += jsonEscape(v.s);
    out += '"';
    return true;
  case Value::Kind::Array: {
    out += '[';
    if (v.items) {
      for (size_t n = 0; n < v.items->size(); ++n) {
        if (n)
          out += ',';
        if (!jsonStringify((*v.items)[n], out, depth + 1, err))
          return false;
      }
    }
    out += ']';
    return true;
  }
  case Value::Kind::Map: {
    out += '{';
    if (v.dict) {
      bool first = true;
      for (const auto &k : v.dict->order) {
        auto it = v.dict->fields.find(k);
        if (it == v.dict->fields.end())
          continue;
        if (!first)
          out += ',';
        first = false;
        out += '"';
        out += jsonEscape(k);
        out += "\":";
        if (!jsonStringify(it->second, out, depth + 1, err))
          return false;
      }
    }
    out += '}';
    return true;
  }
  case Value::Kind::Struct: {
    out += '{';
    if (v.rec) {
      bool first = true;
      for (const auto &k : v.rec->order) {
        auto it = v.rec->fields.find(k);
        if (it == v.rec->fields.end())
          continue;
        if (!first)
          out += ',';
        first = false;
        out += '"';
        out += jsonEscape(k);
        out += "\":";
        if (!jsonStringify(it->second, out, depth + 1, err))
          return false;
      }
    }
    out += '}';
    return true;
  }
  case Value::Kind::Enum:
    out += '"';
    out += jsonEscape(v.variant);
    out += '"';
    return true;
  default:
    err = "cannot stringify value";
    return false;
  }
}

} // namespace

Value Interpreter::callBuiltin(const std::string &module, const std::string &name,
                  const std::vector<Value> &args, int line, int col) {
  if (module == "Future") {
    if (name == "all" || name == "race") {
      if (args.size() != 1)
        runtime("Future." + name + " takes 1 argument", line, col);
      if (args[0].kind != Value::Kind::Array || !args[0].items)
        runtime("Future." + name + " expects Array of Future", line, col);
      const auto &items = *args[0].items;
      for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].kind != Value::Kind::Future || !items[i].fut)
          runtime("Future." + name + " expects Array of Future", line, col);
      }
      auto out = std::make_shared<FutureData>();
      if (items.empty()) {
        if (name == "all")
          settleFuture(out, Value::makeArray());
        else
          failFuture(out, Value::makeString("Future.race on empty Array"), line,
                     col);
        return Value::makeFuture(std::move(out));
      }
      std::vector<std::shared_ptr<FutureData>> futs;
      futs.reserve(items.size());
      for (const auto &item : items)
        futs.push_back(item.fut);
      if (name == "all") {
        struct AllState {
          std::shared_ptr<FutureData> out;
          std::vector<Value> results;
          size_t remaining = 0;
          bool done = false;
        };
        auto state = std::make_shared<AllState>();
        state->out = out;
        state->results.resize(futs.size());
        state->remaining = futs.size();
        for (size_t i = 0; i < futs.size(); ++i) {
          watchFuture(futs[i], [this, state, fut = futs[i], i]() {
            if (state->done)
              return;
            if (fut->state == FutureData::State::Failed) {
              state->done = true;
              failFuture(state->out, fut->error, fut->errLine, fut->errCol);
              return;
            }
            if (fut->state != FutureData::State::Ready)
              return;
            state->results[i] = fut->result;
            if (--state->remaining == 0) {
              state->done = true;
              settleFuture(state->out,
                           Value::makeArray(std::move(state->results)));
            }
          });
        }
      } else {
        struct RaceState {
          std::shared_ptr<FutureData> out;
          bool done = false;
        };
        auto state = std::make_shared<RaceState>();
        state->out = out;
        for (const auto &fut : futs) {
          watchFuture(fut, [this, state, fut]() {
            if (state->done)
              return;
            if (fut->state == FutureData::State::Failed) {
              state->done = true;
              failFuture(state->out, fut->error, fut->errLine, fut->errCol);
              return;
            }
            if (fut->state != FutureData::State::Ready)
              return;
            state->done = true;
            settleFuture(state->out, fut->result);
          });
        }
      }
      return Value::makeFuture(std::move(out));
    }
    runtime("unknown function Future." + name, line, col);
  }
  if (module.empty() && name == "print") {
    std::string parts;
    for (size_t i = 0; i < args.size(); ++i) {
      if (i)
        parts += " ";
      parts += args[i].toString();
    }
    out += parts;
    out += "\n";
    return Value::makeVoid();
  }
  if (module.empty() && name == "assert") {
    if (args.size() != 1)
      runtime("assert takes 1 argument", line, col);
    if (!args[0].truthy())
      runtime("assertion failed", line, col);
    return Value::makeVoid();
  }
  if (module.empty() && name == "len") {
    if (args.size() != 1)
      runtime("len takes 1 argument", line, col);
    if (args[0].kind == Value::Kind::Array)
      return arrayLen(args[0]);
    if (args[0].kind == Value::Kind::Map)
      return mapLen(args[0]);
    if (args[0].kind == Value::Kind::String)
      return Value::makeInt(static_cast<long long>(args[0].s.size()));
    runtime("len expects Array, String, or Map", line, col);
  }
  auto argvAt = [&](long long i) {
    if (i < 0 || static_cast<size_t>(i) >= argv.size())
      runtime("argv index out of range", line, col);
    return Value::makeString(argv[static_cast<size_t>(i)]);
  };
  if ((module.empty() && name == "argv") ||
      (module == "process" && name == "argv")) {
    if (args.size() != 1)
      runtime(module.empty() ? "argv takes 1 argument"
                             : "process.argv takes 1 argument (index)",
              line, col);
    if (args[0].kind != Value::Kind::Int)
      runtime("argv index must be Int", line, col);
    return argvAt(args[0].i);
  }
  if ((module.empty() && name == "argv_len") ||
      (module == "process" && name == "argc")) {
    if (!args.empty())
      runtime(module.empty() ? "argv_len takes 0 arguments"
                             : "process.argc takes 0 arguments",
              line, col);
    return Value::makeInt(static_cast<long long>(argv.size()));
  }
  if (module == "checks") {
    auto need = [&](size_t n) {
      if (args.size() != n)
        runtime("checks." + name + " takes " + std::to_string(n) +
                    " argument(s)",
                line, col);
    };
    if (name == "eq") {
      need(2);
      if (!isNumeric(args[0]) || !isNumeric(args[1]))
        runtime("checks.eq expects numbers", line, col);
      if (!args[0].equals(args[1]))
        runtime("assertion failed", line, col);
      return Value::makeVoid();
    }
    if (name == "neq") {
      need(2);
      if (!isNumeric(args[0]) || !isNumeric(args[1]))
        runtime("checks.neq expects numbers", line, col);
      if (args[0].equals(args[1]))
        runtime("assertion failed", line, col);
      return Value::makeVoid();
    }
    if (name == "eq_string") {
      need(2);
      if (args[0].kind != Value::Kind::String ||
          args[1].kind != Value::Kind::String)
        runtime("checks.eq_string expects String, String", line, col);
      if (args[0].s != args[1].s)
        runtime("assertion failed", line, col);
      return Value::makeVoid();
    }
    if (name == "that" || name == "truthy") {
      need(1);
      if (!args[0].truthy())
        runtime("assertion failed", line, col);
      return Value::makeVoid();
    }
    runtime("unknown function checks." + name, line, col);
  }
  if (module == "__math") {
    static std::mt19937 rng{std::random_device{}()};
    auto needNum = [&](size_t i) {
      if (!isNumeric(args[i]))
        runtime("__math." + name + " expects Float", line, col);
      return asF64(args[i]);
    };
    if (name == "pow") {
      if (args.size() != 2)
        runtime("__math.pow takes 2 arguments", line, col);
      if (args[0].kind != Value::Kind::Int || args[1].kind != Value::Kind::Int)
        runtime("__math.pow expects Int, Int", line, col);
      if (args[1].i < 0)
        return Value::makeInt(0);
      long long r = 1;
      long long base = args[0].i;
      unsigned long long exp = static_cast<unsigned long long>(args[1].i);
      while (exp) {
        if (exp & 1ULL)
          r *= base;
        exp >>= 1;
        if (exp)
          base *= base;
      }
      return Value::makeInt(r);
    }
    if (name == "rand_int") {
      if (args.size() != 1)
        runtime("__math.rand_int takes 1 argument", line, col);
      if (args[0].kind != Value::Kind::Int)
        runtime("__math.rand_int expects Int", line, col);
      if (args[0].i <= 0)
        runtime("__math.rand_int expects n > 0", line, col);
      std::uniform_int_distribution<long long> dist(0, args[0].i - 1);
      return Value::makeInt(dist(rng));
    }
    if (name == "random") {
      if (!args.empty())
        runtime("__math.random takes 0 arguments", line, col);
      std::uniform_real_distribution<double> dist(0.0, 1.0);
      return Value::makeFloat(dist(rng));
    }
    if (name == "sin" || name == "cos" || name == "sqrt" || name == "floor" ||
        name == "ceil" || name == "to_int" || name == "to_float") {
      if (args.size() != 1)
        runtime("__math." + name + " takes 1 argument", line, col);
      if (name == "to_float") {
        if (args[0].kind != Value::Kind::Int)
          runtime("__math.to_float expects Int", line, col);
        return Value::makeFloat(static_cast<double>(args[0].i));
      }
      const double n = needNum(0);
      if (name == "sin")
        return Value::makeFloat(std::sin(n));
      if (name == "cos")
        return Value::makeFloat(std::cos(n));
      if (name == "sqrt") {
        if (n < 0.0)
          runtime("__math.sqrt expects n >= 0", line, col);
        return Value::makeFloat(std::sqrt(n));
      }
      if (name == "floor")
        return Value::makeFloat(std::floor(n));
      if (name == "ceil")
        return Value::makeFloat(std::ceil(n));
      return Value::makeInt(static_cast<long long>(n));
    }
    if (name == "atan2" || name == "powf") {
      if (args.size() != 2)
        runtime("__math." + name + " takes 2 arguments", line, col);
      const double a = needNum(0);
      const double b = needNum(1);
      if (name == "atan2")
        return Value::makeFloat(std::atan2(a, b));
      return Value::makeFloat(std::pow(a, b));
    }
    runtime("unknown function __math." + name, line, col);
  }
  if (module == "__str") {
    auto needStr = [&](size_t i) {
      if (args[i].kind != Value::Kind::String)
        runtime("__str." + name + " expects String", line, col);
      return args[i].s;
    };
    auto needInt = [&](size_t i) {
      if (args[i].kind != Value::Kind::Int)
        runtime("__str." + name + " expects Int", line, col);
      return args[i].i;
    };
    if (name == "contains") {
      if (args.size() != 2)
        runtime("str.contains takes 2 arguments", line, col);
      return Value::makeBool(needStr(0).find(needStr(1)) != std::string::npos);
    }
    if (name == "starts_with") {
      if (args.size() != 2)
        runtime("str.starts_with takes 2 arguments", line, col);
      const std::string s = needStr(0);
      const std::string p = needStr(1);
      return Value::makeBool(s.size() >= p.size() &&
                             s.compare(0, p.size(), p) == 0);
    }
    if (name == "ends_with") {
      if (args.size() != 2)
        runtime("str.ends_with takes 2 arguments", line, col);
      const std::string s = needStr(0);
      const std::string p = needStr(1);
      return Value::makeBool(s.size() >= p.size() &&
                             s.compare(s.size() - p.size(), p.size(), p) == 0);
    }
    if (name == "length") {
      if (args.size() != 1)
        runtime("str.length takes 1 argument", line, col);
      return Value::makeInt(static_cast<long long>(needStr(0).size()));
    }
    if (name == "is_empty") {
      if (args.size() != 1)
        runtime("str.is_empty takes 1 argument", line, col);
      return Value::makeBool(needStr(0).empty());
    }
    if (name == "repeat") {
      if (args.size() != 2)
        runtime("str.repeat takes 2 arguments", line, col);
      const std::string s = needStr(0);
      long long n = needInt(1);
      if (n < 0)
        n = 0;
      if (n > 0 && s.size() > 0) {
        const unsigned long long need =
            static_cast<unsigned long long>(s.size()) *
            static_cast<unsigned long long>(n);
        if (need > kMaxRepeatBytes)
          runtime("str.repeat result too large", line, col);
      }
      std::string out;
      out.reserve(s.size() * static_cast<size_t>(n));
      for (long long i = 0; i < n; ++i)
        out += s;
      return Value::makeString(std::move(out));
    }
    if (name == "upper" || name == "lower") {
      if (args.size() != 1)
        runtime("str." + name + " takes 1 argument", line, col);
      std::string s = needStr(0);
      for (char &c : s) {
        unsigned char u = static_cast<unsigned char>(c);
        c = static_cast<char>(name == "upper" ? std::toupper(u)
                                              : std::tolower(u));
      }
      return Value::makeString(std::move(s));
    }
    if (name == "trim") {
      if (args.size() != 1)
        runtime("str.trim takes 1 argument", line, col);
      const std::string s = needStr(0);
      size_t a = 0;
      while (a < s.size() &&
             std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
      size_t b = s.size();
      while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
      return Value::makeString(s.substr(a, b - a));
    }
    if (name == "slice") {
      if (args.size() != 3)
        runtime("str.slice takes 3 arguments", line, col);
      const std::string s = needStr(0);
      long long start = needInt(1);
      long long end = needInt(2);
      const long long len = static_cast<long long>(s.size());
      if (start < 0)
        start = 0;
      if (start > len)
        start = len;
      if (end < 0)
        end = 0;
      if (end > len)
        end = len;
      if (end < start)
        end = start;
      return Value::makeString(s.substr(static_cast<size_t>(start),
                                        static_cast<size_t>(end - start)));
    }
    if (name == "split") {
      if (args.size() != 2)
        runtime("str.split takes 2 arguments", line, col);
      const std::string s = needStr(0);
      const std::string sep = needStr(1);
      std::vector<Value> parts;
      if (sep.empty()) {
        parts.reserve(s.size());
        for (char c : s)
          parts.push_back(Value::makeString(std::string(1, c)));
      } else {
        size_t start = 0;
        while (true) {
          const size_t pos = s.find(sep, start);
          if (pos == std::string::npos) {
            parts.push_back(Value::makeString(s.substr(start)));
            break;
          }
          parts.push_back(Value::makeString(s.substr(start, pos - start)));
          start = pos + sep.size();
        }
      }
      return Value::makeArray(std::move(parts));
    }
    if (name == "replace") {
      if (args.size() != 3)
        runtime("str.replace takes 3 arguments", line, col);
      std::string s = needStr(0);
      const std::string from = needStr(1);
      const std::string to = needStr(2);
      if (!from.empty()) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
          s.replace(pos, from.size(), to);
          pos += to.size();
        }
      }
      return Value::makeString(std::move(s));
    }
    if (name == "find") {
      if (args.size() != 2)
        runtime("str.find takes 2 arguments", line, col);
      const size_t pos = needStr(0).find(needStr(1));
      if (pos == std::string::npos)
        return Value::makeInt(-1);
      return Value::makeInt(static_cast<long long>(pos));
    }
    runtime("unknown function __str." + name, line, col);
  }
  if (module == "__io") {
    namespace fs = std::filesystem;
    auto needPath = [&](size_t i) {
      if (args[i].kind != Value::Kind::String)
        runtime("__io." + name + " expects String", line, col);
      return args[i].s;
    };
    auto ioThrow = [&](const std::string &msg) {
      throw ThrowEscape{Value::makeString(msg), line, col};
    };
    auto readAll = [&](const std::string &path) {
      std::ifstream in(path, std::ios::binary);
      if (!in)
        ioThrow("cannot read '" + path + "'");
      std::ostringstream ss;
      ss << in.rdbuf();
      return ss.str();
    };
    if (name == "exists") {
      if (args.size() != 1)
        runtime("io.exists takes 1 argument", line, col);
      std::error_code ec;
      return Value::makeBool(fs::exists(needPath(0), ec) && !ec);
    }
    if (name == "remove") {
      if (args.size() != 1)
        runtime("io.remove takes 1 argument", line, col);
      std::error_code ec;
      const bool ok = fs::remove(needPath(0), ec);
      return Value::makeBool(ok && !ec);
    }
    if (name == "read_text") {
      if (args.size() != 1)
        runtime("io.read_text takes 1 argument", line, col);
      return Value::makeString(readAll(needPath(0)));
    }
    if (name == "read_lines") {
      if (args.size() != 1)
        runtime("io.read_lines takes 1 argument", line, col);
      const std::string text = readAll(needPath(0));
      std::vector<Value> lines;
      size_t start = 0;
      for (size_t i = 0; i <= text.size(); ++i) {
        if (i != text.size() && text[i] != '\n')
          continue;
        if (i == text.size() && start == i)
          break;
        std::string line = text.substr(start, i - start);
        if (!line.empty() && line.back() == '\r')
          line.pop_back();
        lines.push_back(Value::makeString(std::move(line)));
        start = i + 1;
      }
      return Value::makeArray(std::move(lines));
    }
    if (name == "write_text") {
      if (args.size() != 2)
        runtime("io.write_text takes 2 arguments", line, col);
      if (args[1].kind != Value::Kind::String)
        runtime("io.write_text expects String", line, col);
      const std::string path = needPath(0);
      std::ofstream outf(path, std::ios::binary);
      if (!outf)
        ioThrow("cannot write '" + path + "'");
      outf << args[1].s;
      if (!outf)
        ioThrow("cannot write '" + path + "'");
      return Value::makeVoid();
    }
    runtime("unknown function __io." + name, line, col);
  }
  if (module == "__uuid") {
    auto hex32 = [](const std::string &s, std::string &hex) {
      hex.clear();
      hex.reserve(32);
      for (unsigned char c : s) {
        if (c == '-')
          continue;
        if (!std::isxdigit(c))
          return false;
        hex.push_back(static_cast<char>(std::tolower(c)));
      }
      return hex.size() == 32;
    };
    auto dashed = [](const std::string &hex) {
      return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" +
             hex.substr(12, 4) + "-" + hex.substr(16, 4) + "-" +
             hex.substr(20, 12);
    };
    if (name == "v4") {
      if (!args.empty())
        runtime("__uuid.v4 takes 0 arguments", line, col);
      static std::mt19937 rng{std::random_device{}()};
      std::uniform_int_distribution<int> dist(0, 15);
      const char *digits = "0123456789abcdef";
      std::string hex;
      hex.reserve(32);
      for (int i = 0; i < 32; ++i) {
        int n = dist(rng);
        if (i == 12)
          n = 4;
        else if (i == 16)
          n = (n & 0x3) | 0x8;
        hex.push_back(digits[n]);
      }
      return Value::makeString(dashed(hex));
    }
    if (name == "parse" || name == "valid") {
      if (args.size() != 1)
        runtime("__uuid." + name + " takes 1 argument", line, col);
      if (args[0].kind != Value::Kind::String)
        runtime("__uuid." + name + " expects String", line, col);
      std::string hex;
      if (!hex32(args[0].s, hex)) {
        if (name == "valid")
          return Value::makeBool(false);
        throw ThrowEscape{Value::makeString("invalid UUID"), line, col};
      }
      if (name == "valid")
        return Value::makeBool(true);
      return Value::makeString(dashed(hex));
    }
    runtime("unknown function __uuid." + name, line, col);
  }
  if (module == "__time") {
    if (name == "now") {
      if (!args.empty())
        runtime("__time.now takes 0 arguments", line, col);
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch());
      return Value::makeInt(ms.count());
    }
    if (name == "sleep") {
      if (args.size() != 1)
        runtime("__time.sleep takes 1 argument", line, col);
      if (args[0].kind != Value::Kind::Int)
        runtime("__time.sleep expects Int", line, col);
      long long ms = args[0].i;
      if (ms < 0)
        ms = 0;
      if (ms > kMaxSleepMs)
        runtime("time.sleep duration too large", line, col);
      std::this_thread::sleep_for(std::chrono::milliseconds(ms));
      return Value::makeVoid();
    }
    if (name == "delay") {
      if (args.size() != 1)
        runtime("__time.delay takes 1 argument", line, col);
      if (args[0].kind != Value::Kind::Int)
        runtime("__time.delay expects Int", line, col);
      long long ms = args[0].i;
      if (ms < 0)
        ms = 0;
      if (ms > kMaxSleepMs)
        runtime("time.delay duration too large", line, col);
      auto fut = std::make_shared<FutureData>();
      const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
      timers.push_back(TimerJob{now + ms, fut});
      return Value::makeFuture(std::move(fut));
    }
    runtime("unknown function __time." + name, line, col);
  }
  if (module == "__path") {
    namespace fs = std::filesystem;
    auto needStr = [&](size_t i) {
      if (args[i].kind != Value::Kind::String)
        runtime("__path." + name + " expects String", line, col);
      return args[i].s;
    };
    if (name == "join") {
      if (args.size() != 2)
        runtime("__path.join takes 2 arguments", line, col);
      return Value::makeString((fs::path(needStr(0)) / needStr(1)).generic_string());
    }
    if (name == "parent") {
      if (args.size() != 1)
        runtime("__path.parent takes 1 argument", line, col);
      return Value::makeString(fs::path(needStr(0)).parent_path().generic_string());
    }
    if (name == "stem") {
      if (args.size() != 1)
        runtime("__path.stem takes 1 argument", line, col);
      return Value::makeString(fs::path(needStr(0)).stem().generic_string());
    }
    runtime("unknown function __path." + name, line, col);
  }
  if (module == "__json") {
    if (name == "parse") {
      if (args.size() != 1)
        runtime("__json.parse takes 1 argument", line, col);
      if (args[0].kind != Value::Kind::String)
        runtime("__json.parse expects String", line, col);
      return JsonParser(args[0].s, line, col).parse();
    }
    if (name == "valid") {
      if (args.size() != 1)
        runtime("__json.valid takes 1 argument", line, col);
      if (args[0].kind != Value::Kind::String)
        runtime("__json.valid expects String", line, col);
      try {
        JsonParser(args[0].s, line, col).parse();
        return Value::makeBool(true);
      } catch (const ThrowEscape &) {
        return Value::makeBool(false);
      }
    }
    if (name == "stringify") {
      if (args.size() != 1)
        runtime("__json.stringify takes 1 argument", line, col);
      std::string out;
      std::string err;
      if (!jsonStringify(args[0], out, 0, err))
        runtime(err, line, col);
      return Value::makeString(std::move(out));
    }
    runtime("unknown function __json." + name, line, col);
  }
  if (module == "__regex") {
    auto needStr = [&](size_t i) {
      if (args[i].kind != Value::Kind::String)
        runtime("__regex." + name + " expects String", line, col);
      return args[i].s;
    };
    auto makeRe = [&](const std::string &pattern) {
      try {
        return std::regex(pattern);
      } catch (const std::regex_error &e) {
        runtime(std::string("invalid regex: ") + e.what(), line, col);
      }
      return std::regex(); // unreachable
    };
    if (name == "valid") {
      if (args.size() != 1)
        runtime("regex.valid takes 1 argument", line, col);
      try {
        std::regex re(needStr(0));
        (void)re;
        return Value::makeBool(true);
      } catch (const std::regex_error &) {
        return Value::makeBool(false);
      }
    }
    if (name == "is_match") {
      if (args.size() != 2)
        runtime("regex.is_match takes 2 arguments", line, col);
      const std::string pattern = needStr(0);
      const std::string text = needStr(1);
      const std::regex re = makeRe(pattern);
      return Value::makeBool(std::regex_search(text, re));
    }
    if (name == "find") {
      if (args.size() != 2)
        runtime("regex.find takes 2 arguments", line, col);
      const std::string pattern = needStr(0);
      const std::string text = needStr(1);
      const std::regex re = makeRe(pattern);
      std::smatch m;
      if (!std::regex_search(text, m, re))
        return Value::makeInt(-1);
      return Value::makeInt(static_cast<long long>(m.position()));
    }
    if (name == "find_match") {
      if (args.size() != 2)
        runtime("regex.find_match takes 2 arguments", line, col);
      const std::string pattern = needStr(0);
      const std::string text = needStr(1);
      const std::regex re = makeRe(pattern);
      std::smatch m;
      if (!std::regex_search(text, m, re))
        return Value::makeString("");
      return Value::makeString(m.str());
    }
    if (name == "captures") {
      if (args.size() != 2)
        runtime("regex.captures takes 2 arguments", line, col);
      const std::string pattern = needStr(0);
      const std::string text = needStr(1);
      const std::regex re = makeRe(pattern);
      std::smatch m;
      if (!std::regex_search(text, m, re))
        return Value::makeArray({});
      std::vector<Value> parts;
      parts.reserve(m.size());
      for (size_t i = 0; i < m.size(); ++i)
        parts.push_back(Value::makeString(m[i].str()));
      return Value::makeArray(std::move(parts));
    }
    if (name == "findall") {
      if (args.size() != 2)
        runtime("regex.findall takes 2 arguments", line, col);
      const std::string pattern = needStr(0);
      const std::string text = needStr(1);
      const std::regex re = makeRe(pattern);
      std::vector<Value> parts;
      for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end;
           ++it)
        parts.push_back(Value::makeString(it->str()));
      return Value::makeArray(std::move(parts));
    }
    if (name == "replace") {
      if (args.size() != 3)
        runtime("regex.replace takes 3 arguments", line, col);
      const std::string pattern = needStr(0);
      const std::string text = needStr(1);
      const std::string with = needStr(2);
      const std::regex re = makeRe(pattern);
      return Value::makeString(std::regex_replace(text, re, with));
    }
    if (name == "split") {
      if (args.size() != 2)
        runtime("regex.split takes 2 arguments", line, col);
      const std::string pattern = needStr(0);
      const std::string text = needStr(1);
      const std::regex re = makeRe(pattern);
      std::vector<Value> parts;
      std::sregex_token_iterator it(text.begin(), text.end(), re, -1);
      std::sregex_token_iterator end;
      for (; it != end; ++it)
        parts.push_back(Value::makeString(it->str()));
      if (parts.empty())
        parts.push_back(Value::makeString(text));
      return Value::makeArray(std::move(parts));
    }
    runtime("unknown function __regex." + name, line, col);
  }
  if (module == "__ui")
    return uiHostCall(*this, name, args, line, col);
  runtime(module.empty() ? "unknown function '" + name + "'"
                         : "unknown function " + module + "." + name,
          line, col);
}

Value Interpreter::runUserBody(const FnDecl &fn, const std::vector<Value> &args,
                               int line, int col,
                               const std::map<std::string, Binding> *caps) {
  if (debug.stack.size() >= kMaxCallDepth)
    runtime("call stack overflow", line, col);
  struct ModGuard {
    Interpreter *self;
    std::string prev;
    ModGuard(Interpreter *s, std::string name) : self(s), prev(s->currentModule) {
      self->currentModule = std::move(name);
    }
    ~ModGuard() { self->currentModule = std::move(prev); }
  } guard(this, fn.module);
  if (fn.isDeprecated)
    out += "warning: '" + fn.name + "' is deprecated\n";
  if (args.size() != fn.params.size()) {
    runtime(fn.name + " takes " + std::to_string(fn.params.size()) +
                " argument(s)",
            line, col);
  }
  env.emplace_back();
  struct EnvPop {
    Interpreter *self;
    explicit EnvPop(Interpreter *s) : self(s) {}
    ~EnvPop() { self->env.pop_back(); }
  } pop(this);
  if (caps)
    env.back() = *caps;
  for (size_t i = 0; i < fn.params.size(); ++i)
    env.back()[fn.params[i]] = Binding{args[i], false};
  DebugFrame frame;
  frame.name = fn.name;
  frame.path = !fn.file.empty() ? fn.file : file;
  frame.line = fn.line > 0 ? fn.line : 1;
  frame.col = 1;
  frame.envIndex = env.size() - 1;
  debug.stack.push_back(frame);
  struct StackPop {
    Interpreter *self;
    explicit StackPop(Interpreter *s) : self(s) {}
    ~StackPop() {
      if (!self->debug.stack.empty())
        self->debug.stack.pop_back();
    }
  } stackPop(this);
  Value ret = Value::makeVoid();
  Flow f = execBlock(fn.body);
  if (f.kind == Flow::Kind::Throw)
    throw ThrowEscape{std::move(f.value), line, col};
  if (f.kind == Flow::Kind::Return)
    ret = f.value;
  else if (f.kind == Flow::Kind::Break)
    runtime("break outside loop", fn.line, 1);
  else if (f.kind == Flow::Kind::Continue)
    runtime("continue outside loop", fn.line, 1);
  return ret;
}

Value Interpreter::callUser(const FnDecl &fn, const std::vector<Value> &args, int line,
               int col, const std::map<std::string, Binding> *caps) {
  if (fn.isAsync) {
    auto fut = std::make_shared<FutureData>();
    std::vector<Value> capturedArgs = args;
    std::shared_ptr<std::map<std::string, Binding>> capturedCaps;
    if (caps)
      capturedCaps = std::make_shared<std::map<std::string, Binding>>(*caps);
    const FnDecl *fnPtr = &fn;
    enqueueMicrotask([this, fut, fnPtr, capturedArgs, capturedCaps, line,
                      col]() {
      try {
        Value ret =
            runUserBody(*fnPtr, capturedArgs, line, col,
                        capturedCaps ? capturedCaps.get() : nullptr);
        settleFuture(fut, std::move(ret));
      } catch (ThrowEscape &ex) {
        failFuture(fut, std::move(ex.value), ex.line, ex.col);
      }
    });
    return Value::makeFuture(std::move(fut));
  }
  return runUserBody(fn, args, line, col, caps);
}

void Interpreter::enqueueMicrotask(std::function<void()> fn) {
  microtasks.push_back(std::move(fn));
}

void Interpreter::settleFuture(const std::shared_ptr<FutureData> &fut,
                               Value value) {
  if (!fut || fut->state != FutureData::State::Pending)
    return;
  fut->state = FutureData::State::Ready;
  fut->result = std::move(value);
  auto waiters = std::move(fut->waiters);
  fut->waiters.clear();
  for (auto &fn : waiters)
    enqueueMicrotask(std::move(fn));
}

void Interpreter::failFuture(const std::shared_ptr<FutureData> &fut, Value error,
                             int line, int col) {
  if (!fut || fut->state != FutureData::State::Pending)
    return;
  fut->state = FutureData::State::Failed;
  fut->error = std::move(error);
  fut->errLine = line;
  fut->errCol = col;
  auto waiters = std::move(fut->waiters);
  fut->waiters.clear();
  for (auto &fn : waiters)
    enqueueMicrotask(std::move(fn));
}

void Interpreter::watchFuture(const std::shared_ptr<FutureData> &fut,
                              std::function<void()> fn) {
  if (!fut) {
    enqueueMicrotask(std::move(fn));
    return;
  }
  if (fut->state != FutureData::State::Pending) {
    enqueueMicrotask(std::move(fn));
    return;
  }
  fut->waiters.push_back(std::move(fn));
}

void Interpreter::cancelFuture(const std::shared_ptr<FutureData> &fut, int line,
                               int col) {
  if (!fut || fut->state != FutureData::State::Pending)
    return;
  for (auto it = timers.begin(); it != timers.end();) {
    if (it->future == fut)
      it = timers.erase(it);
    else
      ++it;
  }
  for (auto it = frameJobs.begin(); it != frameJobs.end();) {
    if (it->future == fut)
      it = frameJobs.erase(it);
    else
      ++it;
  }
  failFuture(fut, Value::makeString("cancelled"), line, col);
}

bool Interpreter::pumpEventLoopOnce(bool mayWait) {
  flushDeferred();
  bool progress = false;
  while (!microtasks.empty()) {
    progress = true;
    auto fn = std::move(microtasks.front());
    microtasks.erase(microtasks.begin());
    fn();
    flushDeferred();
  }
  const auto nowMs = [&]() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  };
  long long now = nowMs();
  std::vector<std::shared_ptr<FutureData>> due;
  for (auto it = timers.begin(); it != timers.end();) {
    if (it->deadlineMs <= now) {
      due.push_back(it->future);
      it = timers.erase(it);
    } else {
      ++it;
    }
  }
  for (auto &fut : due) {
    progress = true;
    settleFuture(fut, Value::makeVoid());
  }
  if (uiPumpFrameJobs(*this))
    progress = true;
  if (progress)
    return true;
  if (!mayWait || timers.empty())
    return false;
  long long next = timers.front().deadlineMs;
  for (const auto &t : timers) {
    if (t.deadlineMs < next)
      next = t.deadlineMs;
  }
  now = nowMs();
  if (next > now)
    std::this_thread::sleep_for(std::chrono::milliseconds(next - now));
  now = nowMs();
  for (auto it = timers.begin(); it != timers.end();) {
    if (it->deadlineMs <= now) {
      settleFuture(it->future, Value::makeVoid());
      it = timers.erase(it);
      progress = true;
    } else {
      ++it;
    }
  }
  return progress || !microtasks.empty();
}

void Interpreter::drainEventLoop() {
  for (int i = 0; i < 100000; ++i) {
    if (!pumpEventLoopOnce(false)) {
      if (microtasks.empty() && timers.empty() && frameJobs.empty())
        return;
      if (!pumpEventLoopOnce(true))
        return;
    }
  }
  runtime("event loop exceeded iteration limit", 1, 1);
}

Value Interpreter::awaitFuture(const std::shared_ptr<FutureData> &fut, int line,
                               int col) {
  if (!fut)
    runtime("can only await a Future", line, col);
  while (fut->state == FutureData::State::Pending) {
    if (!pumpEventLoopOnce(true))
      runtime("await deadlock: Future never settles", line, col);
  }
  if (fut->state == FutureData::State::Failed)
    throw ThrowEscape{fut->error, fut->errLine, fut->errCol};
  return fut->result;
}

Value Interpreter::evalLambda(const Expr &e) {
  if (!e.lambda)
    runtime("invalid closure", e.line, e.col);
  std::set<std::string> bound;
  for (const auto &p : e.lambda->params)
    bound.insert(p);
  std::set<std::string> free;
  for (const auto &st : e.lambda->body)
    collectFreeStmt(st, bound, free);
  auto clo = std::make_shared<ClosureData>();
  clo->fn = e.lambda;
  bool capturedSelf = false;
  for (const auto &name : free) {
    if (findLocalFn(name) || fns.count(name) || structs.count(name) ||
        allTypes.count(name) || enums.count(name) || traits.count(name) ||
        signalArity.count(name) || findModuleBind(name) || isHostModule(name) ||
        findEnum(name))
      continue;
    if (Binding *b = findLocalBinding(name)) {
      clo->caps[name] = *b;
      if (name == "self")
        capturedSelf = true;
      continue;
    }
    if (fieldOnSelf(name) ||
        (findLocalBinding("self") && findLocalBinding("self")->value.rec &&
         lookupTypeSignal(findLocalBinding("self")->value.rec->name, name))) {
      if (!capturedSelf) {
        if (Binding *self = findLocalBinding("self")) {
          clo->caps["self"] = *self;
          capturedSelf = true;
        }
      }
    }
  }
  return Value::makeClosure(std::move(clo));
}

Value Interpreter::callFnValue(const Value &fn, const std::vector<Value> &args, int line,
                  int col) {
  if (fn.kind != Value::Kind::FnRef)
    runtime("can only call a function", line, col);
  if (fn.clo) {
    if (!fn.clo->fn)
      runtime("invalid closure", line, col);
    return callUser(*fn.clo->fn, args, line, col, &fn.clo->caps);
  }
  auto it = fns.find(fn.s);
  if (it == fns.end()) {
    if (FnDecl *local = findLocalFn(fn.s))
      return callUser(*local, args, line, col);
    runtime("undefined function '" + fn.s + "'", line, col);
  }
  return callUser(*it->second, args, line, col);
}

Value Interpreter::invokeTypeMethod(const Value &obj, const std::string &startType,
                       const std::string &name,
                       const std::vector<Value> &args, int line, int col) {
  auto found = lookupMethod(startType, name);
  if (!found.second) {
    if (FnDecl *fn = findUfcs(name, startType))
      return callUfcs(*fn, obj, args, line, col);
    runtime("struct " + startType + " has no method '" + name + "'", line,
            col);
  }
  const std::string &definedOn = found.first;
  const FnDecl &fn = *found.second;
  if (fn.isAbstract)
    runtime("cannot call abstract method '" + name + "'", line, col);
  if (args.size() != fn.params.size() - 1) {
    runtime(startType + "." + name + " takes " +
                std::to_string(fn.params.size() - 1) + " argument(s)",
            line, col);
  }
  std::string prev = superType;
  auto pit = classParents.find(definedOn);
  superType = pit != classParents.end() ? typeHead(pit->second) : "";
  struct SuperGuard {
    Interpreter *self;
    std::string prev;
    SuperGuard(Interpreter *s, std::string p)
        : self(s), prev(std::move(p)) {}
    ~SuperGuard() { self->superType = std::move(prev); }
  } guard(this, std::move(prev));
  std::vector<Value> all;
  all.reserve(args.size() + 1);
  all.push_back(obj);
  all.insert(all.end(), args.begin(), args.end());
  return callUser(fn, all, line, col);
}

Value Interpreter::callTypeMethod(const Value &obj, const std::string &name,
                     const std::vector<Value> &args, int line, int col) {
  return invokeTypeMethod(obj, obj.rec->name, name, args, line, col);
}

Value Interpreter::callSignal(const std::string &signal, const std::string &name,
                 const std::vector<Value> &args, int line, int col) {
  auto it = signalArity.find(signal);
  if (it == signalArity.end())
    runtime("undefined signal '" + signal + "'", line, col);
  return callSignalList(signal, it->second, listeners[signal], nullptr, name,
                        args, line, col);
}

Value Interpreter::dispatchSignal(const Value &sig, const std::string &name,
                     const std::vector<Value> &args, int line, int col) {
  if (sig.kind != Value::Kind::SignalRef)
    runtime("can only call signal methods on a signal", line, col);
  if (!sig.rec) {
    if (!signalArity.count(sig.s))
      runtime("undefined signal '" + sig.s + "'", line, col);
    return callSignalList(sig.s, signalArity[sig.s], listeners[sig.s], nullptr,
                          name, args, line, col);
  }
  auto arity = lookupTypeSignal(sig.rec->name, sig.s);
  if (!arity)
    runtime("struct " + sig.rec->name + " has no signal '" + sig.s + "'", line,
            col);
  return callSignalList(sig.s, *arity, sig.rec->listeners[sig.s], sig.rec, name,
                        args, line, col);
}

Value Interpreter::callSignalList(const std::string &signal, std::size_t arity,
                     std::vector<Value> &list,
                     std::shared_ptr<StructData> rec, const std::string &name,
                     const std::vector<Value> &args, int line, int col) {
  auto fnArity = [&](const Value &fn) -> std::size_t {
    if (fn.kind != Value::Kind::FnRef)
      runtime("signal '" + signal + "' connect expects a function", line, col);
    if (fn.clo) {
      if (!fn.clo->fn)
        runtime("invalid closure", line, col);
      return fn.clo->fn->params.size();
    }
    auto it = fns.find(fn.s);
    if (it != fns.end())
      return it->second->params.size();
    if (FnDecl *local = findLocalFn(fn.s))
      return local->params.size();
    runtime("undefined function '" + fn.s + "'", line, col);
  };
  if (name == "connect") {
    if (args.size() != 1)
      runtime("signal '" + signal + "' connect takes 1 argument", line, col);
    if (args[0].kind != Value::Kind::FnRef)
      runtime("signal '" + signal + "' connect expects a function", line, col);
    if (fnArity(args[0]) != arity)
      runtime("signal '" + signal + "' connect expected " +
                  std::to_string(arity) + " parameter(s)",
              line, col);
    bool found = false;
    for (const auto &fn : list) {
      if (sameFn(fn, args[0])) {
        found = true;
        break;
      }
    }
    if (!found)
      list.push_back(args[0]);
    return Value::makeVoid();
  }
  if (name == "disconnect") {
    if (args.size() != 1)
      runtime("signal '" + signal + "' disconnect takes 1 argument", line,
              col);
    if (args[0].kind != Value::Kind::FnRef)
      runtime("signal '" + signal + "' disconnect expects a function", line,
              col);
    if (!args[0].clo) {
      if (!fns.count(args[0].s) && !findLocalFn(args[0].s))
        runtime("undefined function '" + args[0].s + "'", line, col);
    }
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const Value &fn) { return sameFn(fn, args[0]); }),
               list.end());
    return Value::makeVoid();
  }
  if (name == "emit" || name == "emit_deferred") {
    if (args.size() != arity)
      runtime("signal '" + signal + "' expected " + std::to_string(arity) +
                  " argument(s), got " + std::to_string(args.size()),
              line, col);
    if (name == "emit_deferred") {
      DeferredEmit item;
      item.signal = signal;
      item.rec = std::move(rec);
      item.args = args;
      item.line = line;
      item.col = col;
      deferred.push_back(std::move(item));
      return Value::makeVoid();
    }
    if (syncEmitDepth >= kMaxSyncEmitDepth)
      runtime("signal emit nested too deeply", line, col);
    struct EmitDepth {
      int *depth;
      explicit EmitDepth(int *d) : depth(d) { ++(*depth); }
      ~EmitDepth() { --(*depth); }
    } emitGuard(&syncEmitDepth);
    auto copy = list;
    for (const auto &fn : copy)
      callFnValue(fn, args, line, col);
    return Value::makeVoid();
  }
  runtime("signal '" + signal + "' has no method '" + name + "'", line, col);
}

void Interpreter::flushDeferred() {
  int waves = 0;
  while (!deferred.empty()) {
    if (++waves > 64)
      runtime("deferred emit nested too deeply", deferred.front().line,
              deferred.front().col);
    auto batch = std::move(deferred);
    deferred.clear();
    for (auto &item : batch) {
      if (item.rec) {
        auto arity = lookupTypeSignal(item.rec->name, item.signal);
        if (!arity)
          runtime("struct " + item.rec->name + " has no signal '" +
                      item.signal + "'",
                  item.line, item.col);
        auto copy = item.rec->listeners[item.signal];
        for (const auto &fn : copy)
          callFnValue(fn, item.args, item.line, item.col);
      } else {
        auto copy = listeners[item.signal];
        for (const auto &fn : copy)
          callFnValue(fn, item.args, item.line, item.col);
      }
    }
  }
}

Value Interpreter::call(const std::string &module, const std::string &name,
           const std::vector<Value> &args, int line, int col) {
  if (module.empty()) {
    if (FnDecl *fn = findLocalFn(name))
      return callUser(*fn, args, line, col);
    if (Binding *local = findLocalBinding(name)) {
      if (local->value.kind == Value::Kind::FnRef)
        return callFnValue(local->value, args, line, col);
    }
    if (Binding *global = findGlobalBinding(name)) {
      if (global->value.kind == Value::Kind::FnRef)
        return callFnValue(global->value, args, line, col);
    }
    if (Binding *self = findLocalBinding("self")) {
      if (self->value.kind == Value::Kind::Struct && self->value.rec &&
          lookupMethod(self->value.rec->name, name).second)
        return invokeTypeMethod(self->value, self->value.rec->name, name, args,
                                line, col);
    }
  } else if (signalArity.count(module)) {
    return callSignal(module, name, args, line, col);
  }
  return callBuiltin(module, name, args, line, col);
}

Flow Interpreter::execBlock(const std::vector<Stmt> &stmts) {
  for (const auto &stmt : stmts) {
    Flow f = execStmt(stmt);
    if (f.kind != Flow::Kind::Next)
      return f;
  }
  return Flow::next();
}

std::string Interpreter::debugNormPath(std::string p) {
  for (char &c : p) {
    if (c == '/')
      c = '\\';
#ifdef _WIN32
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
#endif
  }
  return p;
}

Value Interpreter::debugEval(const std::string &source, std::string &err) {
  err.clear();
  std::vector<Diagnostic> diags;
  Expr e = parseExprSource(source, "<watch>", &diags);
  if (!diags.empty()) {
    err = diags[0].message;
    return Value::makeVoid();
  }
  try {
    return eval(e);
  } catch (const ThrowEscape &ex) {
    err = "throw: " + ex.value.toString();
    return Value::makeVoid();
  } catch (const std::exception &ex) {
    err = ex.what();
    return Value::makeVoid();
  }
}

void Interpreter::debugCheck(const Stmt &stmt) {
  if (!debug.enabled || debug.abort)
    return;
  if (stmt.kind == Stmt::Kind::Comment)
    return;
  if (debug.stack.empty())
    return;
  DebugFrame &top = debug.stack.back();
  top.line = stmt.line > 0 ? stmt.line : top.line;
  top.col = stmt.col > 0 ? stmt.col : top.col;
  const std::string path = debugNormPath(top.path.empty() ? file : top.path);
  bool stop = false;
  std::string reason;
  auto bit = debug.breakpoints.find(path);
  if (bit != debug.breakpoints.end()) {
    for (const auto &bp : bit->second) {
      if (bp.line != top.line)
        continue;
      if (bp.condition.empty()) {
        stop = true;
        reason = "breakpoint";
        break;
      }
      std::string err;
      Value cond = debugEval(bp.condition, err);
      if (err.empty() && cond.truthy()) {
        stop = true;
        reason = "breakpoint";
        break;
      }
    }
  }
  if (!stop && debug.stopOnEntry && !debug.entrySeen) {
    debug.entrySeen = true;
    stop = true;
    reason = "entry";
  }
  if (!stop) {
    switch (debug.mode) {
    case DebugState::Mode::Next:
      if (debug.stack.size() <= debug.stepDepth) {
        stop = true;
        reason = "step";
      }
      break;
    case DebugState::Mode::StepIn:
      stop = true;
      reason = "step";
      break;
    case DebugState::Mode::StepOut:
      if (debug.stack.size() < debug.stepDepth) {
        stop = true;
        reason = "step";
      }
      break;
    case DebugState::Mode::Run:
      break;
    }
  }
  if (!stop)
    return;
  debug.stopPath = top.path.empty() ? file : top.path;
  debug.stopLine = top.line;
  debug.stopCol = top.col;
  if (debug.pauseAndWait)
    debug.pauseAndWait(reason);
  if (debug.abort)
    throw std::runtime_error("debug session ended");
}

Flow Interpreter::execStmt(const Stmt &stmt) {
  debugCheck(stmt);
  switch (stmt.kind) {
  case Stmt::Kind::Expr:
    eval(stmt.expr);
    return Flow::next();
  case Stmt::Kind::Var:
    env.back()[stmt.name] = Binding{eval(stmt.expr), false};
    return Flow::next();
  case Stmt::Kind::Const:
    env.back()[stmt.name] = Binding{eval(stmt.expr), true};
    return Flow::next();
  case Stmt::Kind::Assign: {
    Value rhs = eval(stmt.expr);
    auto store = [&](Value *slot) {
      if (stmt.op != "=" && !stmt.op.empty())
        *slot = applyAssignOp(stmt.op, *slot, rhs, stmt.line, stmt.col);
      else
        *slot = std::move(rhs);
    };
    if (Binding *slot = findLocalBinding(stmt.name)) {
      if (slot->isConst)
        runtime("cannot assign to const '" + stmt.name + "'", stmt.line,
                stmt.col);
      store(&slot->value);
      return Flow::next();
    }
    if (Value *field = fieldOnSelf(stmt.name)) {
      if (StructData *rec = selfRec()) {
        if (isDataType(rec->name))
          runtime("cannot assign to data field '" + stmt.name + "'", stmt.line,
                  stmt.col);
      }
      store(field);
      return Flow::next();
    }
    Binding *slot = findGlobalBinding(stmt.name);
    if (!slot)
      runtime("undefined variable '" + stmt.name + "'", stmt.line, stmt.col);
    if (slot->isConst)
      runtime("cannot assign to const '" + stmt.name + "'", stmt.line,
              stmt.col);
    store(&slot->value);
    return Flow::next();
  }
  case Stmt::Kind::FieldAssign: {
    Value obj;
    if (stmt.target.kind == Expr::Kind::Var && stmt.target.text == "super")
      obj = selfObject(stmt.line, stmt.col);
    else
      obj = eval(stmt.target);
    if (obj.kind != Value::Kind::Struct || !obj.rec)
      runtime("cannot assign field on " + obj.toString(), stmt.line, stmt.col);
    if (!obj.rec->fields.count(stmt.name))
      runtime("struct " + obj.rec->name + " has no field '" + stmt.name + "'",
              stmt.line, stmt.col);
    if (isDataType(obj.rec->name))
      runtime("cannot assign to data field '" + stmt.name + "'", stmt.line,
              stmt.col);
    Value rhs = eval(stmt.expr);
    if (stmt.op != "=" && !stmt.op.empty())
      obj.rec->fields[stmt.name] = applyAssignOp(
          stmt.op, obj.rec->fields[stmt.name], rhs, stmt.line, stmt.col);
    else
      obj.rec->fields[stmt.name] = std::move(rhs);
    return Flow::next();
  }
  case Stmt::Kind::IndexAssign: {
    if (stmt.target.kids.size() < 2)
      runtime("invalid index assignment", stmt.line, stmt.col);
    Value obj = eval(stmt.target.kids[0]);
    Value idx = eval(stmt.target.kids[1]);
    Value rhs = eval(stmt.expr);
    if (stmt.op != "=" && !stmt.op.empty())
      rhs = applyAssignOp(stmt.op, indexGet(obj, idx, stmt.line, stmt.col), rhs,
                          stmt.line, stmt.col);
    indexSet(obj, idx, std::move(rhs), stmt.line, stmt.col);
    return Flow::next();
  }
  case Stmt::Kind::Return:
    return Flow::ret(eval(stmt.expr));
  case Stmt::Kind::If:
    if (eval(stmt.expr).truthy())
      return execBlock(stmt.body);
    return execBlock(stmt.elseBody);
  case Stmt::Kind::While: {
    struct DepthGuard {
      int *d;
      explicit DepthGuard(int *x) : d(x) { ++*d; }
      ~DepthGuard() { --*d; }
    } depth(&loopDepth);
    while (eval(stmt.expr).truthy()) {
      Flow f = execBlock(stmt.body);
      if (f.kind == Flow::Kind::Break)
        break;
      if (f.kind == Flow::Kind::Continue)
        continue;
      if (f.kind == Flow::Kind::Return || f.kind == Flow::Kind::Throw)
        return f;
    }
    return Flow::next();
  }
  case Stmt::Kind::For: {
    std::vector<Value> items = iterItems(eval(stmt.expr), stmt.line, stmt.col);
    const bool existed = env.back().count(stmt.name) != 0;
    Binding saved;
    if (existed)
      saved = env.back()[stmt.name];
    struct DepthGuard {
      int *d;
      explicit DepthGuard(int *x) : d(x) { ++*d; }
      ~DepthGuard() { --*d; }
    } depth(&loopDepth);
    Flow result = Flow::next();
    for (const auto &item : items) {
      env.back()[stmt.name] = Binding{item, false};
      Flow f = execBlock(stmt.body);
      if (f.kind == Flow::Kind::Break)
        break;
      if (f.kind == Flow::Kind::Continue)
        continue;
      if (f.kind == Flow::Kind::Return || f.kind == Flow::Kind::Throw) {
        result = f;
        break;
      }
    }
    if (existed)
      env.back()[stmt.name] = saved;
    else
      env.back().erase(stmt.name);
    return result;
  }
  case Stmt::Kind::Match: {
    Value value = eval(stmt.expr);
    for (const auto &arm : stmt.arms) {
      bool hit = false;
      if (arm.pat == MatchArm::Pat::Wildcard)
        hit = true;
      else if (arm.pat == MatchArm::Pat::Int)
        hit = value.kind == Value::Kind::Int && value.i == arm.number;
      else if (arm.pat == MatchArm::Pat::Float)
        hit = isNumeric(value) &&
              numericEq(asF64(value), arm.real);
      else if (arm.pat == MatchArm::Pat::String)
        hit = value.kind == Value::Kind::String && value.s == arm.text;
      else if (arm.pat == MatchArm::Pat::Bool)
        hit = value.kind == Value::Kind::Bool && value.b == arm.boolean;
      else if (arm.pat == MatchArm::Pat::Variant) {
        if (value.kind != Value::Kind::Enum)
          runtime("match pattern '" + arm.name +
                      "' does not match value of type " + value.toString(),
                  stmt.line, stmt.col);
        hit = value.variant == arm.name;
      }
      if (!hit)
        continue;
      std::map<std::string, Binding> saved;
      std::vector<std::string> added;
      auto bindName = [&](const std::string &name, const Value &v) {
        if (name.empty() || name == "_")
          return;
        if (env.back().count(name))
          saved[name] = env.back()[name];
        else
          added.push_back(name);
        env.back()[name] = Binding{v, false};
      };
      if (arm.pat == MatchArm::Pat::Variant && hit) {
        const EnumDecl *en = findEnum(value.s);
        const EnumVariant *var = nullptr;
        if (en) {
          for (const auto &v : en->variants) {
            if (v.name == value.variant) {
              var = &v;
              break;
            }
          }
        }
        bool named = false;
        for (const auto &f : arm.fieldNames) {
          if (!f.empty())
            named = true;
        }
        if (named) {
          if (!var)
            runtime("undefined enum '" + value.s + "'", stmt.line, stmt.col);
          for (size_t i = 0; i < arm.fieldNames.size(); ++i) {
            const std::string &field = arm.fieldNames[i];
            if (field.empty())
              continue;
            size_t idx = static_cast<size_t>(-1);
            for (size_t fi = 0; fi < var->fieldNames.size(); ++fi) {
              if (var->fieldNames[fi] == field) {
                idx = fi;
                break;
              }
            }
            if (idx == static_cast<size_t>(-1))
              runtime(value.s + "." + value.variant + " has no field '" +
                          field + "'",
                      stmt.line, stmt.col);
            if (idx >= value.payload.size())
              runtime(value.s + "." + value.variant + " field '" + field +
                          "' is missing",
                      stmt.line, stmt.col);
            bindName(arm.binds[i], value.payload[idx]);
          }
        } else if (arm.binds.size() == 1) {
          if (value.payload.size() == 1)
            bindName(arm.binds[0], value.payload[0]);
          else
            bindName(arm.binds[0], Value::makeArray(value.payload));
        } else {
          for (size_t i = 0; i < arm.binds.size(); ++i) {
            if (i < value.payload.size())
              bindName(arm.binds[i], value.payload[i]);
          }
        }
      }
      Flow f = execBlock(arm.body);
      for (const auto &kv : saved)
        env.back()[kv.first] = kv.second;
      for (const auto &n : added)
        env.back().erase(n);
      return f;
    }
    return Flow::next();
  }
  case Stmt::Kind::Pass:
    return Flow::next();
  case Stmt::Kind::Comment:
    return Flow::next();
  case Stmt::Kind::Throw:
    return Flow::thr(eval(stmt.expr));
  case Stmt::Kind::Do: {
    auto handle = [&](Value err) -> Flow {
      if (stmt.elseBody.empty() && stmt.name.empty())
        return Flow::thr(std::move(err));
      std::map<std::string, Binding> saved;
      bool added = false;
      if (!stmt.name.empty()) {
        if (env.back().count(stmt.name))
          saved[stmt.name] = env.back()[stmt.name];
        else
          added = true;
        env.back()[stmt.name] = Binding{std::move(err), false};
      }
      Flow c = execBlock(stmt.elseBody);
      if (!stmt.name.empty()) {
        if (!saved.empty())
          env.back()[stmt.name] = saved[stmt.name];
        else if (added)
          env.back().erase(stmt.name);
      }
      return c;
    };
    try {
      Flow f = execBlock(stmt.body);
      if (f.kind == Flow::Kind::Throw)
        return handle(std::move(f.value));
      return f;
    } catch (ThrowEscape &ex) {
      return handle(std::move(ex.value));
    }
  }
  case Stmt::Kind::Break:
    if (loopDepth == 0)
      runtime("break outside loop", stmt.line, stmt.col);
    return Flow::brk();
  case Stmt::Kind::Continue:
    if (loopDepth == 0)
      runtime("continue outside loop", stmt.line, stmt.col);
    return Flow::cont();
  }
  return Flow::next();
}

Value Interpreter::callNamed(const std::string &name) {
  auto it = fns.find(name);
  if (it == fns.end())
    throw std::runtime_error(
        locatedError("runtime error", file, 0, 0,
                     "unknown function '" + name + "'"));
  try {
    Value ret = callUser(*it->second, {}, it->second->line, 1);
    if (ret.kind == Value::Kind::Future && ret.fut)
      ret = awaitFuture(ret.fut, it->second->line, 1);
    flushDeferred();
    drainEventLoop();
    return ret;
  } catch (const ThrowEscape &ex) {
    runtime("uncaught throw: " + ex.value.toString(), ex.line, ex.col);
  }
}
