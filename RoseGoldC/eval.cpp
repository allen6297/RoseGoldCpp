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
  if (!currentModule.empty()) {
    auto lit = loaded.find(currentModule);
    if (lit != loaded.end()) {
      auto it = lit->second.structs.find(name);
      if (it != lit->second.structs.end())
        return it->second;
    }
  }
  auto it = structs.find(name);
  if (it != structs.end())
    return it->second;
  return nullptr;
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
    std::vector<Value> nums;
    nums.reserve(static_cast<size_t>(iter.i));
    for (long long n = 0; n < iter.i; ++n)
      nums.push_back(Value::makeInt(n));
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
    runtime("Map has no method '" + name + "'", line, col);
  }
  if (obj.kind == Value::Kind::String) {
    if (name == "len") {
      if (!args.empty())
        runtime("String.len takes 0 arguments", line, col);
      return Value::makeInt(static_cast<long long>(obj.s.size()));
    }
    runtime("String has no method '" + name + "'", line, col);
  }
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
      return Value::makeInt(-v.i);
    }
    return Value::makeBool(!v.truthy());
  }
  case Expr::Kind::Binary: {
    Value a = eval(e.kids[0]);
    Value b = eval(e.kids[1]);
    if (e.text == "+" && a.kind == Value::Kind::String &&
        b.kind == Value::Kind::String)
      return Value::makeString(a.s + b.s);
    if (e.text == "==")
      return Value::makeBool(a.equals(b));
    if (e.text == "!=")
      return Value::makeBool(!a.equals(b));
    if (!isNumeric(a) || !isNumeric(b))
      runtime("operands must be numbers", e.line, e.col);
    const bool bothInt =
        a.kind == Value::Kind::Int && b.kind == Value::Kind::Int;
    if (e.text == "+") {
      if (bothInt)
        return Value::makeInt(a.i + b.i);
      return Value::makeFloat(asF64(a) + asF64(b));
    }
    if (e.text == "-") {
      if (bothInt)
        return Value::makeInt(a.i - b.i);
      return Value::makeFloat(asF64(a) - asF64(b));
    }
    if (e.text == "*") {
      if (bothInt)
        return Value::makeInt(a.i * b.i);
      return Value::makeFloat(asF64(a) * asF64(b));
    }
    if (e.text == "/") {
      if (bothInt) {
        if (b.i == 0)
          runtime("division by zero", e.line, e.col);
        return Value::makeInt(a.i / b.i);
      }
      if (asF64(b) == 0.0)
        runtime("division by zero", e.line, e.col);
      return Value::makeFloat(asF64(a) / asF64(b));
    }
    if (e.text == "%") {
      if (bothInt) {
        if (b.i == 0)
          runtime("modulo by zero", e.line, e.col);
        return Value::makeInt(a.i % b.i);
      }
      if (asF64(b) == 0.0)
        runtime("modulo by zero", e.line, e.col);
      return Value::makeFloat(std::fmod(asF64(a), asF64(b)));
    }
    if (e.text == "<") {
      if (bothInt)
        return Value::makeBool(a.i < b.i);
      return Value::makeBool(asF64(a) < asF64(b));
    }
    if (e.text == ">") {
      if (bothInt)
        return Value::makeBool(a.i > b.i);
      return Value::makeBool(asF64(a) > asF64(b));
    }
    runtime("unknown operator", e.line, e.col);
  }
  case Expr::Kind::Call: {
    std::vector<Value> args;
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
      if (signalArity.count(recv.text))
        return callSignal(recv.text, e.text, args, e.line, e.col);
      if (const std::string *modName = findModuleBind(recv.text))
        return callModule(*modName, e.text, args, e.line, e.col);
      if (findLocalBinding(recv.text) || fieldOnSelf(recv.text) ||
          findGlobalBinding(recv.text) || findEnum(recv.text)) {
        Value obj = resolveValue(recv.text, recv.line, recv.col);
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
            obj.kind == Value::Kind::Map)
          return callValueMethod(obj, e.text, args, e.line, e.col);
        runtime("cannot call method '" + e.text + "' on " + obj.toString(),
                e.line, e.col);
      }
      return callBuiltin(recv.text, e.text, args, e.line, e.col);
    }
    if (std::optional<std::string> modName = modulePath(recv))
      return callModule(*modName, e.text, args, e.line, e.col);
    Value obj = eval(recv);
    if (obj.kind == Value::Kind::Struct && obj.rec)
      return callTypeMethod(obj, e.text, args, e.line, e.col);
    if (obj.kind == Value::Kind::EnumType) {
      const EnumDecl *en = findEnum(obj.s);
      if (!en)
        runtime("undefined enum '" + obj.s + "'", e.line, e.col);
      return constructEnum(*en, e.text, std::move(args), e.line, e.col);
    }
    if (obj.kind == Value::Kind::Array || obj.kind == Value::Kind::String ||
        obj.kind == Value::Kind::Map)
      return callValueMethod(obj, e.text, args, e.line, e.col);
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
    if (it == obj.rec->fields.end())
      runtime("struct " + obj.rec->name + " has no field '" + e.text + "'",
              e.line, e.col);
    return it->second;
  }
  case Expr::Kind::StructLit: {
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
      runtime("missing field '" + field + "' on " + decl.name, e.line, e.col);
    }
    return Value::makeStruct(std::move(data));
  }
  case Expr::Kind::Array: {
    std::vector<Value> elems;
    elems.reserve(e.kids.size());
    for (const auto &kid : e.kids)
      elems.push_back(eval(kid));
    return Value::makeArray(std::move(elems));
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

Value Interpreter::callBuiltin(const std::string &module, const std::string &name,
                  const std::vector<Value> &args, int line, int col) {
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
      static std::mt19937 rng{std::random_device{}()};
      std::uniform_int_distribution<long long> dist(0, args[0].i - 1);
      return Value::makeInt(dist(rng));
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
    runtime("unknown function __str." + name, line, col);
  }
  runtime(module.empty() ? "unknown function '" + name + "'"
                         : "unknown function " + module + "." + name,
          line, col);
}

Value Interpreter::callUser(const FnDecl &fn, const std::vector<Value> &args, int line,
               int col) {
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
  for (size_t i = 0; i < fn.params.size(); ++i)
    env.back()[fn.params[i]] = Binding{args[i], false};
  Value ret = Value::makeVoid();
  Flow f = execBlock(fn.body);
  if (f.kind == Flow::Kind::Return)
    ret = f.value;
  else if (f.kind == Flow::Kind::Break)
    runtime("break outside loop", fn.line, 1);
  else if (f.kind == Flow::Kind::Continue)
    runtime("continue outside loop", fn.line, 1);
  env.pop_back();
  return ret;
}

Value Interpreter::invokeTypeMethod(const Value &obj, const std::string &startType,
                       const std::string &name,
                       const std::vector<Value> &args, int line, int col) {
  auto found = lookupMethod(startType, name);
  if (!found.second)
    runtime("struct " + startType + " has no method '" + name + "'", line,
            col);
  const std::string &definedOn = found.first;
  const FnDecl &fn = *found.second;
  if (args.size() != fn.params.size() - 1) {
    runtime(startType + "." + name + " takes " +
                std::to_string(fn.params.size() - 1) + " argument(s)",
            line, col);
  }
  std::string prev = superType;
  auto pit = classParents.find(definedOn);
  superType = pit != classParents.end() ? pit->second : "";
  std::vector<Value> all;
  all.reserve(args.size() + 1);
  all.push_back(obj);
  all.insert(all.end(), args.begin(), args.end());
  Value result = callUser(fn, all, line, col);
  superType = std::move(prev);
  return result;
}

Value Interpreter::callTypeMethod(const Value &obj, const std::string &name,
                     const std::vector<Value> &args, int line, int col) {
  return invokeTypeMethod(obj, obj.rec->name, name, args, line, col);
}

Value Interpreter::callSignal(const std::string &signal, const std::string &name,
                 const std::vector<Value> &args, int line, int col) {
  if (name == "connect") {
    if (args.size() != 1)
      runtime("signal '" + signal + "' connect takes 1 argument", line, col);
    if (args[0].kind != Value::Kind::FnRef)
      runtime("signal '" + signal + "' connect expects a function", line, col);
    const std::string &fnName = args[0].s;
    auto fnIt = fns.find(fnName);
    if (fnIt == fns.end())
      runtime("undefined function '" + fnName + "'", line, col);
    if (fnIt->second->params.size() != signalArity[signal])
      runtime("signal '" + signal + "' connect expected " +
                  std::to_string(signalArity[signal]) + " parameter(s)",
              line, col);
    auto &list = listeners[signal];
    if (std::find(list.begin(), list.end(), fnName) == list.end())
      list.push_back(fnName);
    return Value::makeVoid();
  }
  if (name == "disconnect") {
    if (args.size() != 1)
      runtime("signal '" + signal + "' disconnect takes 1 argument", line,
              col);
    if (args[0].kind != Value::Kind::FnRef)
      runtime("signal '" + signal + "' disconnect expects a function", line,
              col);
    const std::string &fnName = args[0].s;
    if (!fns.count(fnName))
      runtime("undefined function '" + fnName + "'", line, col);
    auto &list = listeners[signal];
    list.erase(std::remove(list.begin(), list.end(), fnName), list.end());
    return Value::makeVoid();
  }
  if (name == "emit") {
    if (args.size() != signalArity[signal])
      runtime("signal '" + signal + "' expected " +
                  std::to_string(signalArity[signal]) + " argument(s), got " +
                  std::to_string(args.size()),
              line, col);
    auto list = listeners[signal];
    for (const auto &fnName : list)
      callUser(*fns[fnName], args, line, col);
    return Value::makeVoid();
  }
  runtime("signal '" + signal + "' has no method '" + name + "'", line, col);
}

Value Interpreter::call(const std::string &module, const std::string &name,
           const std::vector<Value> &args, int line, int col) {
  if (module.empty()) {
    if (FnDecl *fn = findLocalFn(name))
      return callUser(*fn, args, line, col);
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

Flow Interpreter::execStmt(const Stmt &stmt) {
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
    if (Binding *slot = findLocalBinding(stmt.name)) {
      if (slot->isConst)
        runtime("cannot assign to const '" + stmt.name + "'", stmt.line,
                stmt.col);
      slot->value = std::move(rhs);
      return Flow::next();
    }
    if (Value *field = fieldOnSelf(stmt.name)) {
      *field = std::move(rhs);
      return Flow::next();
    }
    Binding *slot = findGlobalBinding(stmt.name);
    if (!slot)
      runtime("undefined variable '" + stmt.name + "'", stmt.line, stmt.col);
    if (slot->isConst)
      runtime("cannot assign to const '" + stmt.name + "'", stmt.line,
              stmt.col);
    slot->value = std::move(rhs);
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
    obj.rec->fields[stmt.name] = eval(stmt.expr);
    return Flow::next();
  }
  case Stmt::Kind::IndexAssign: {
    if (stmt.target.kids.size() < 2)
      runtime("invalid index assignment", stmt.line, stmt.col);
    Value obj = eval(stmt.target.kids[0]);
    Value idx = eval(stmt.target.kids[1]);
    indexSet(obj, idx, eval(stmt.expr), stmt.line, stmt.col);
    return Flow::next();
  }
  case Stmt::Kind::Return:
    return Flow::ret(eval(stmt.expr));
  case Stmt::Kind::If:
    if (eval(stmt.expr).truthy())
      return execBlock(stmt.body);
    return execBlock(stmt.elseBody);
  case Stmt::Kind::While: {
    ++loopDepth;
    while (eval(stmt.expr).truthy()) {
      Flow f = execBlock(stmt.body);
      if (f.kind == Flow::Kind::Break)
        break;
      if (f.kind == Flow::Kind::Continue)
        continue;
      if (f.kind == Flow::Kind::Return) {
        --loopDepth;
        return f;
      }
    }
    --loopDepth;
    return Flow::next();
  }
  case Stmt::Kind::For: {
    std::vector<Value> items = iterItems(eval(stmt.expr), stmt.line, stmt.col);
    const bool existed = env.back().count(stmt.name) != 0;
    Binding saved;
    if (existed)
      saved = env.back()[stmt.name];
    ++loopDepth;
    Flow result = Flow::next();
    for (const auto &item : items) {
      env.back()[stmt.name] = Binding{item, false};
      Flow f = execBlock(stmt.body);
      if (f.kind == Flow::Kind::Break)
        break;
      if (f.kind == Flow::Kind::Continue)
        continue;
      if (f.kind == Flow::Kind::Return) {
        result = f;
        break;
      }
    }
    --loopDepth;
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
  return callUser(*it->second, {}, it->second->line, 1);
}
