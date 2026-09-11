#pragma once

#include <string>
#include <vector>

struct Expr {
  enum class Kind {
    Int,
    String,
    Bool,
    Var,
    Unary,
    Binary,
    Call,
    MethodCall,
    Member,
    StructLit
  } kind{};
  long long number = 0;
  bool boolean = false;
  std::string text;
  std::string module;
  std::vector<std::string> names;
  std::vector<Expr> kids;
  int line = 1;
  int col = 1;
};

struct Stmt {
  enum class Kind {
    Expr,
    Var,
    Const,
    Assign,
    FieldAssign,
    Return,
    If,
    While,
    Pass,
    Break,
    Continue
  } kind{};
  std::string name;
  Expr expr;
  Expr target;
  std::vector<Stmt> body;
  std::vector<Stmt> elseBody;
  int line = 1;
  int col = 1;
};

struct FnDecl {
  std::string name;
  std::vector<std::string> params;
  std::vector<Stmt> body;
  std::string module;
  bool isTest = false;
  bool isDeprecated = false;
  bool isPub = true;
  int line = 1;
};

struct structDecl {
  std::string name;
  std::vector<std::string> fields;
  std::vector<FnDecl> methods;
  bool isPub = true;
  int line = 1;
};

struct ImplDecl {
  std::string typeName;
  std::string traitName;
  std::vector<FnDecl> methods;
  int line = 1;
};

struct ClassField {
  std::string name;
  bool hasDefault = false;
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
  int line = 1;
};

struct TraitMethod {
  std::string name;
  std::vector<std::string> params;
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
  std::vector<ImplDecl> impls;
  std::vector<SignalDecl> signals;
  std::vector<ModDecl> mods;
};
