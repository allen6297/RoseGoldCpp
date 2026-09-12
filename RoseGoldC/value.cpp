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

std::string Value::toString() const {
  switch (kind) {
  case Kind::Void:
    return "";
  case Kind::Bool:
    return b ? "true" : "false";
  case Kind::Int:
    return std::to_string(i);
  case Kind::Float: {
    std::ostringstream ss;
    ss << real;
    return ss.str();
  }
  case Kind::String:
    return s;
  case Kind::FnRef:
    return clo ? "<fn>" : s;
  case Kind::Struct: {
    if (!rec)
      return "{}";
    std::string parts;
    for (const auto &f : rec->order) {
      auto it = rec->fields.find(f);
      if (it == rec->fields.end())
        continue;
      if (!parts.empty())
        parts += ", ";
      parts += f + ": " + it->second.toString();
    }
    return rec->name + " { " + parts + " }";
  }
  case Kind::Array: {
    std::string out = "[";
    if (items) {
      for (size_t n = 0; n < items->size(); ++n) {
        if (n)
          out += ", ";
        out += (*items)[n].toString();
      }
    }
    out += "]";
    return out;
  }
  case Kind::Map: {
    std::string out = "{";
    if (dict) {
      for (size_t n = 0; n < dict->order.size(); ++n) {
        const std::string &k = dict->order[n];
        auto it = dict->fields.find(k);
        if (it == dict->fields.end())
          continue;
        if (n)
          out += ", ";
        out += "\"" + k + "\": " + it->second.toString();
      }
    }
    out += "}";
    return out;
  }
  case Kind::Range: {
    long long end = payload.empty() ? 0 : payload[0].i;
    return std::to_string(i) + (b ? "..=" : "..") + std::to_string(end);
  }
  case Kind::EnumType:
    return "enum " + s;
  case Kind::Enum: {
    std::string out = s + "." + variant;
    if (payload.empty())
      return out;
    out += "(";
    for (size_t i = 0; i < payload.size(); ++i) {
      if (i)
        out += ", ";
      out += payload[i].toString();
    }
    out += ")";
    return out;
  }
  case Kind::SignalRef:
    if (rec)
      return rec->name + "." + s;
    return s;
  }
  return "";
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
  if (kind != other.kind) {
    if (isNumeric(*this) && isNumeric(other))
      return numericEq(asF64(*this), asF64(other));
    return false;
  }
  switch (kind) {
  case Kind::Void:
    return true;
  case Kind::Bool:
    return b == other.b;
  case Kind::Int:
    return i == other.i;
  case Kind::Float:
    return numericEq(real, other.real);
  case Kind::String:
    return s == other.s;
  case Kind::FnRef:
    if (clo || other.clo)
      return clo.get() == other.clo.get();
    return s == other.s;
  case Kind::Struct: {
    if (rec == other.rec)
      return true;
    if (!rec || !other.rec)
      return false;
    if (rec->name != other.rec->name)
      return false;
    if (rec->fields.size() != other.rec->fields.size())
      return false;
    for (const auto &kv : rec->fields) {
      auto it = other.rec->fields.find(kv.first);
      if (it == other.rec->fields.end() || !kv.second.equals(it->second))
        return false;
    }
    return true;
  }
  case Kind::Array: {
    if (items == other.items)
      return true;
    if (!items || !other.items)
      return false;
    if (items->size() != other.items->size())
      return false;
    for (size_t n = 0; n < items->size(); ++n) {
      if (!(*items)[n].equals((*other.items)[n]))
        return false;
    }
    return true;
  }
  case Kind::Map: {
    if (dict == other.dict)
      return true;
    if (!dict || !other.dict)
      return false;
    if (dict->fields.size() != other.dict->fields.size())
      return false;
    for (const auto &kv : dict->fields) {
      auto it = other.dict->fields.find(kv.first);
      if (it == other.dict->fields.end() || !kv.second.equals(it->second))
        return false;
    }
    return true;
  }
  case Kind::Range: {
    long long end = payload.empty() ? 0 : payload[0].i;
    long long otherEnd =
        other.payload.empty() ? 0 : other.payload[0].i;
    return i == other.i && b == other.b && end == otherEnd;
  }
  case Kind::EnumType:
    return s == other.s;
  case Kind::Enum: {
    if (s != other.s || variant != other.variant ||
        payload.size() != other.payload.size())
      return false;
    for (size_t i = 0; i < payload.size(); ++i) {
      if (!payload[i].equals(other.payload[i]))
        return false;
    }
    return true;
  }
  case Kind::SignalRef:
    return s == other.s && rec == other.rec;
  }
  return false;
}

std::string readFile(const std::string &path) {
  std::ifstream in(path);
  if (!in)
    throw std::runtime_error("failed to read " + path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

