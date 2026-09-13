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

namespace {

std::string toStringRec(const Value &v, std::set<const void *> &seen);
bool equalsRec(const Value &a, const Value &b, std::set<const void *> &seenA,
               std::set<const void *> &seenB);

std::string toStringRec(const Value &v, std::set<const void *> &seen) {
  switch (v.kind) {
  case Value::Kind::Void:
    return "";
  case Value::Kind::Bool:
    return v.b ? "true" : "false";
  case Value::Kind::Int:
    return std::to_string(v.i);
  case Value::Kind::Float: {
    std::ostringstream ss;
    ss << v.real;
    return ss.str();
  }
  case Value::Kind::String:
    return v.s;
  case Value::Kind::FnRef:
    return v.clo ? "<fn>" : v.s;
  case Value::Kind::Struct: {
    if (!v.rec)
      return "{}";
    const void *id = v.rec.get();
    if (!seen.insert(id).second)
      return v.rec->name + " { ... }";
    std::string parts;
    for (const auto &f : v.rec->order) {
      auto it = v.rec->fields.find(f);
      if (it == v.rec->fields.end())
        continue;
      if (!parts.empty())
        parts += ", ";
      parts += f + ": " + toStringRec(it->second, seen);
    }
    seen.erase(id);
    return v.rec->name + " { " + parts + " }";
  }
  case Value::Kind::Array: {
    if (!v.items)
      return "[]";
    const void *id = v.items.get();
    if (!seen.insert(id).second)
      return "[...]";
    std::string out = "[";
    for (size_t n = 0; n < v.items->size(); ++n) {
      if (n)
        out += ", ";
      out += toStringRec((*v.items)[n], seen);
    }
    out += "]";
    seen.erase(id);
    return out;
  }
  case Value::Kind::Map: {
    if (!v.dict)
      return "{}";
    const void *id = v.dict.get();
    if (!seen.insert(id).second)
      return "{...}";
    std::string out = "{";
    for (size_t n = 0; n < v.dict->order.size(); ++n) {
      const std::string &k = v.dict->order[n];
      auto it = v.dict->fields.find(k);
      if (it == v.dict->fields.end())
        continue;
      if (n)
        out += ", ";
      out += "\"" + k + "\": " + toStringRec(it->second, seen);
    }
    out += "}";
    seen.erase(id);
    return out;
  }
  case Value::Kind::Range: {
    long long end = v.payload.empty() ? 0 : v.payload[0].i;
    return std::to_string(v.i) + (v.b ? "..=" : "..") + std::to_string(end);
  }
  case Value::Kind::EnumType:
    return "enum " + v.s;
  case Value::Kind::Enum: {
    std::string out = v.s + "." + v.variant;
    if (v.payload.empty())
      return out;
    out += "(";
    for (size_t i = 0; i < v.payload.size(); ++i) {
      if (i)
        out += ", ";
      out += toStringRec(v.payload[i], seen);
    }
    out += ")";
    return out;
  }
  case Value::Kind::SignalRef:
    if (v.rec)
      return v.rec->name + "." + v.s;
    return v.s;
  case Value::Kind::Future:
    return "<Future>";
  }
  return "";
}

bool equalsRec(const Value &a, const Value &b, std::set<const void *> &seenA,
               std::set<const void *> &seenB) {
  if (a.kind != b.kind) {
    if (isNumeric(a) && isNumeric(b))
      return numericEq(asF64(a), asF64(b));
    return false;
  }
  switch (a.kind) {
  case Value::Kind::Void:
    return true;
  case Value::Kind::Bool:
    return a.b == b.b;
  case Value::Kind::Int:
    return a.i == b.i;
  case Value::Kind::Float:
    return numericEq(a.real, b.real);
  case Value::Kind::String:
    return a.s == b.s;
  case Value::Kind::FnRef:
    if (a.clo || b.clo)
      return a.clo.get() == b.clo.get();
    return a.s == b.s;
  case Value::Kind::Struct: {
    if (a.rec == b.rec)
      return true;
    if (!a.rec || !b.rec)
      return false;
    if (a.rec->name != b.rec->name)
      return false;
    if (a.rec->fields.size() != b.rec->fields.size())
      return false;
    const void *ida = a.rec.get();
    const void *idb = b.rec.get();
    if (!seenA.insert(ida).second || !seenB.insert(idb).second)
      return ida == idb;
    for (const auto &kv : a.rec->fields) {
      auto it = b.rec->fields.find(kv.first);
      if (it == b.rec->fields.end() ||
          !equalsRec(kv.second, it->second, seenA, seenB)) {
        seenA.erase(ida);
        seenB.erase(idb);
        return false;
      }
    }
    seenA.erase(ida);
    seenB.erase(idb);
    return true;
  }
  case Value::Kind::Array: {
    if (a.items == b.items)
      return true;
    if (!a.items || !b.items)
      return false;
    if (a.items->size() != b.items->size())
      return false;
    const void *ida = a.items.get();
    const void *idb = b.items.get();
    if (!seenA.insert(ida).second || !seenB.insert(idb).second)
      return ida == idb;
    for (size_t n = 0; n < a.items->size(); ++n) {
      if (!equalsRec((*a.items)[n], (*b.items)[n], seenA, seenB)) {
        seenA.erase(ida);
        seenB.erase(idb);
        return false;
      }
    }
    seenA.erase(ida);
    seenB.erase(idb);
    return true;
  }
  case Value::Kind::Map: {
    if (a.dict == b.dict)
      return true;
    if (!a.dict || !b.dict)
      return false;
    if (a.dict->fields.size() != b.dict->fields.size())
      return false;
    const void *ida = a.dict.get();
    const void *idb = b.dict.get();
    if (!seenA.insert(ida).second || !seenB.insert(idb).second)
      return ida == idb;
    for (const auto &kv : a.dict->fields) {
      auto it = b.dict->fields.find(kv.first);
      if (it == b.dict->fields.end() ||
          !equalsRec(kv.second, it->second, seenA, seenB)) {
        seenA.erase(ida);
        seenB.erase(idb);
        return false;
      }
    }
    seenA.erase(ida);
    seenB.erase(idb);
    return true;
  }
  case Value::Kind::Range: {
    long long end = a.payload.empty() ? 0 : a.payload[0].i;
    long long otherEnd = b.payload.empty() ? 0 : b.payload[0].i;
    return a.i == b.i && a.b == b.b && end == otherEnd;
  }
  case Value::Kind::EnumType:
    return a.s == b.s;
  case Value::Kind::Enum: {
    if (a.s != b.s || a.variant != b.variant ||
        a.payload.size() != b.payload.size())
      return false;
    for (size_t i = 0; i < a.payload.size(); ++i) {
      if (!equalsRec(a.payload[i], b.payload[i], seenA, seenB))
        return false;
    }
    return true;
  }
  case Value::Kind::SignalRef:
    return a.s == b.s && a.rec == b.rec;
  case Value::Kind::Future:
    return a.fut == b.fut;
  }
  return false;
}

} // namespace

std::string Value::toString() const {
  std::set<const void *> seen;
  return toStringRec(*this, seen);
}

bool Value::truthy() const {
  switch (kind) {
  case Kind::Bool:
    return b;
  case Kind::Int:
    return i != 0;
  case Kind::Float:
    return real != 0.0;
  case Kind::String:
    return !s.empty();
  case Kind::FnRef:
  case Kind::Struct:
  case Kind::Array:
  case Kind::Map:
  case Kind::Range:
  case Kind::EnumType:
  case Kind::Enum:
  case Kind::SignalRef:
  case Kind::Future:
    return true;
  default:
    return false;
  }
}

bool numericEq(double a, double b) {
  return a == b || std::fabs(a - b) < 1e-9;
}

bool isNumeric(const Value &v) {
  return v.kind == Value::Kind::Int || v.kind == Value::Kind::Float;
}

double asF64(const Value &v) {
  return v.kind == Value::Kind::Float ? v.real : static_cast<double>(v.i);
}

bool Value::equals(const Value &other) const {
  std::set<const void *> seenA;
  std::set<const void *> seenB;
  return equalsRec(*this, other, seenA, seenB);
}

std::string readFile(const std::string &path) {
  std::ifstream in(path);
  if (!in)
    throw std::runtime_error("failed to read " + path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
