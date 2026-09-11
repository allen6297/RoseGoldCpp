#pragma once

#include <string>
#include <vector>

enum class Vis : char { Pub = 0, Protected = 1, Private = 2 };

inline const char *visName(Vis v) {
  if (v == Vis::Private)
    return "private";
  if (v == Vis::Protected)
    return "protected";
  return "pub";
}

struct Expr {
  enum class Kind {
    Int,
    Float,
    String,
    Bool,
    Var,
    Unary,
    Binary,
    Call,
    MethodCall,
    Member,
    Index,
    Array,
    Map,
    StructLit,
    Range,
    Try
  } kind{};
  long long number = 0;
  double real = 0;
  bool boolean = false;
  std::string text;
  std::string module;
  std::vector<std::string> names;
  std::vector<Expr> kids;
  int line = 1;
  int col = 1;
};

struct Stmt;

struct MatchArm {
  enum class Pat { Wildcard, Int, Float, String, Bool, Variant } pat{};
  std::string name;
  long long number = 0;
  double real = 0;
  bool boolean = false;
  std::string text;
  std::vector<std::string> binds;
  std::vector<std::string> fieldNames;
  std::vector<Stmt> body;
};

struct Stmt {
  enum class Kind {
    Expr,
    Var,
    Const,
    Assign,
    FieldAssign,
    IndexAssign,
    Return,
    If,
    While,
    For,
    Match,
    Pass,
    Break,
    Continue,
    Throw,
    Do
  } kind{};
  std::string name;
  Expr expr;
  Expr target;
  std::vector<Stmt> body;
  std::vector<Stmt> elseBody;
  std::vector<MatchArm> arms;
  std::string typeName;
  std::string op = "=";
  int line = 1;
  int col = 1;
};

struct FnDecl {
  std::string name;
  std::vector<std::string> params;
  std::vector<std::string> paramTypes;
  std::string returnType;
  std::vector<Stmt> body;
  std::string module;
  bool isTest = false;
  bool isDeprecated = false;
  bool isConstexpr = false;
  bool isUfcs = false;
  bool throws = false;
  bool isPub = true;
  bool isAbstract = false;
  bool isFinal = false;
  Vis vis = Vis::Pub;
  int line = 1;
};

struct structDecl {
  std::string name;
  std::vector<std::string> fields;
  std::vector<std::string> fieldTypes;
  std::vector<char> fieldOptional;
  std::vector<char> fieldVis;
  std::vector<FnDecl> methods;
  std::vector<std::string> implTraits;
  bool isPub = true;
  bool isData = false;
  int line = 1;

  bool isOptionalField(const std::string &name) const {
    for (size_t i = 0; i < fields.size(); ++i) {
      if (fields[i] != name)
        continue;
      return i < fieldOptional.size() && fieldOptional[i];
    }
    return false;
  }

  std::string typeOfField(const std::string &name) const {
    for (size_t i = 0; i < fields.size(); ++i) {
      if (fields[i] != name)
        continue;
      if (i < fieldTypes.size())
        return fieldTypes[i];
      return "";
    }
    return "";
  }
};

struct ImplDecl {
  std::string typeName;
  std::string traitName;
  std::vector<FnDecl> methods;
  int line = 1;
};

struct ClassField {
  std::string name;
  std::string type;
  bool hasDefault = false;
  bool optional = false;
  Vis vis = Vis::Pub;
  Expr defaultValue;
};

struct NestedImpl {
  std::string traitName;
  std::vector<FnDecl> methods;
};

struct ClassDecl {
  std::string name;
  std::string parent;
  std::vector<std::string> implTraits;
  std::vector<ClassField> fields;
  std::vector<FnDecl> methods;
  std::vector<NestedImpl> traitImpls;
  structDecl shape;
  bool isPub = true;
  bool isAbstract = false;
  bool isFinal = false;
  int line = 1;
};

struct TraitMethod {
  std::string name;
  std::vector<std::string> params;
  std::vector<std::string> paramTypes;
  std::string returnType;
  bool throws = false;
  int line = 1;
};

struct SignalDecl {
  std::string name;
  std::vector<std::string> params;
  bool isPub = true;
  int line = 1;
};

struct TraitDecl {
  std::string name;
  std::vector<TraitMethod> methods;
  std::vector<SignalDecl> signals;
  bool isPub = true;
  int line = 1;
};

struct EnumVariant {
  std::string name;
  std::vector<std::string> fieldNames;
  int arity = 0;
};

struct EnumDecl {
  std::string name;
  std::vector<EnumVariant> variants;
  bool isPub = true;
  int line = 1;
};

struct ImportDecl {
  std::vector<std::string> path;
  std::string alias;
  bool isFrom = false;
  int line = 1;
  int col = 1;
};

struct ModDecl {
  std::string name;
  std::vector<ImportDecl> imports;
  std::vector<FnDecl> fns;
  std::vector<structDecl> structs;
  std::vector<ClassDecl> classes;
  std::vector<TraitDecl> traits;
  std::vector<EnumDecl> enums;
  std::vector<ImplDecl> impls;
  std::vector<SignalDecl> signals;
  std::vector<ModDecl> mods;
  bool isPub = true;
  int line = 1;
};

struct Program {
  std::vector<ImportDecl> imports;
  std::vector<FnDecl> fns;
  std::vector<structDecl> structs;
  std::vector<ClassDecl> classes;
  std::vector<TraitDecl> traits;
  std::vector<EnumDecl> enums;
  std::vector<ImplDecl> impls;
  std::vector<SignalDecl> signals;
  std::vector<ModDecl> mods;
};
