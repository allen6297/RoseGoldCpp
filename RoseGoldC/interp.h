#pragma once

#include "ast.h"
#include "eval.h"
#include "lexer.h"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct StructData;
struct MapData;

struct Value {
  enum class Kind {
    Void,
    Bool,
    Int,
    Float,
    String,
    FnRef,
    Struct,
    Array,
    Map,
    EnumType,
    Enum
  } kind = Kind::Void;
  bool b = false;
  long long i = 0;
  double real = 0;
  std::string s;
  std::string variant;
  std::vector<Value> payload;
  std::shared_ptr<StructData> rec;
  std::shared_ptr<std::vector<Value>> items;
  std::shared_ptr<MapData> dict;

  static Value makeVoid() { return {}; }
  static Value makeBool(bool v) {
    Value x;
    x.kind = Kind::Bool;
    x.b = v;
    return x;
  }
  static Value makeInt(long long v) {
    Value x;
    x.kind = Kind::Int;
    x.i = v;
    return x;
  }
  static Value makeFloat(double v) {
    Value x;
    x.kind = Kind::Float;
    x.real = v;
    return x;
  }
  static Value makeString(std::string v) {
    Value x;
    x.kind = Kind::String;
    x.s = std::move(v);
    return x;
  }
  static Value makeFnRef(std::string name) {
    Value x;
    x.kind = Kind::FnRef;
    x.s = std::move(name);
    return x;
  }
  static Value makeStruct(std::shared_ptr<StructData> data) {
    Value x;
    x.kind = Kind::Struct;
    x.rec = std::move(data);
    return x;
  }
  static Value makeArray(std::vector<Value> elems = {}) {
    Value x;
    x.kind = Kind::Array;
    x.items = std::make_shared<std::vector<Value>>(std::move(elems));
    return x;
  }
  static Value makeMap(std::shared_ptr<MapData> data) {
    Value x;
    x.kind = Kind::Map;
    x.dict = std::move(data);
    return x;
  }
  static Value makeEnumType(std::string name) {
    Value x;
    x.kind = Kind::EnumType;
    x.s = std::move(name);
    return x;
  }
  static Value makeEnum(std::string type, std::string variant,
                        std::vector<Value> payload = {}) {
    Value x;
    x.kind = Kind::Enum;
    x.s = std::move(type);
    x.variant = std::move(variant);
    x.payload = std::move(payload);
    return x;
  }

  std::string toString() const;
  bool truthy() const;
  bool equals(const Value &other) const;
};

struct StructData {
  std::string name;
  std::vector<std::string> order;
  std::map<std::string, Value> fields;
};

struct MapData {
  std::vector<std::string> order;
  std::map<std::string, Value> fields;
};

struct LoadedMod {
  std::map<std::string, FnDecl *> fns;
  std::map<std::string, FnDecl *> exports;
  std::map<std::string, structDecl *> structs;
  std::map<std::string, structDecl *> exportStructs;
  std::map<std::string, EnumDecl *> enums;
  std::map<std::string, EnumDecl *> exportEnums;
  std::map<std::string, std::string> modules;
  std::map<std::string, std::string> exportMods;
  std::map<std::string, FnDecl *> fromFns;
};

inline bool isHostModule(const std::string &name) {
  return name == "checks" || name == "process" || name == "__math" ||
         name == "__str";
}

inline bool isCrateStdlib(const std::string &name) {
  return name == "math" || name == "str";
}

inline ModDecl *pickMod(Program &p, const std::string &name) {
  ModDecl *named = nullptr;
  ModDecl *only = nullptr;
  int modCount = 0;
  const bool hasOther = !p.fns.empty() || !p.structs.empty() ||
                        !p.classes.empty() || !p.traits.empty() ||
                        !p.enums.empty() || !p.impls.empty() ||
                        !p.signals.empty();
  for (auto &m : p.mods) {
    ++modCount;
    only = &m;
    if (m.name == name)
      named = &m;
  }
  if (named)
    return named;
  if (modCount == 1 && !hasOther)
    return only;
  return nullptr;
}

struct Flow {
  enum class Kind { Next, Return, Break, Continue } kind = Kind::Next;
  Value value;

  static Flow next() { return {}; }
  static Flow ret(Value v) {
    Flow f;
    f.kind = Kind::Return;
    f.value = std::move(v);
    return f;
  }
  static Flow brk() {
    Flow f;
    f.kind = Kind::Break;
    return f;
  }
  static Flow cont() {
    Flow f;
    f.kind = Kind::Continue;
    return f;
  }
};

inline RunResult failResult(const std::string &message, int exitCode = 1) {
  RunResult r;
  r.ok = false;
  r.message = message;
  r.exitCode = exitCode;
  return r;
}

struct Binding {
  Value value;
  bool isConst = false;
};

struct Interpreter {
  Program program;
  std::map<std::string, const FnDecl *> fns;
  std::map<std::string, const structDecl *> structs;
  std::map<std::string, const structDecl *> allTypes;
  std::map<std::string, const TraitDecl *> traits;
  std::map<std::string, const EnumDecl *> enums;
  std::map<std::string, std::string> classParents;
  std::map<std::string, std::map<std::string, const Expr *>> fieldDefaults;
  std::map<std::string, std::vector<std::string>> typeTraits;
  std::map<std::string, std::map<std::string, const FnDecl *>> typeMethods;
  std::map<std::string, std::size_t> signalArity;
  std::map<std::string, std::vector<std::string>> listeners;
  std::vector<std::map<std::string, Binding>> env;
  int loopDepth = 0;
  std::string file;
  std::vector<std::string> argv;
  std::string out;
  std::vector<std::unique_ptr<Program>> extras;
  std::map<std::string, LoadedMod> loaded;
  std::map<std::string, std::string> moduleBinds;
  std::vector<std::string> loading;
  std::string currentModule;
  std::string superType;

  explicit Interpreter(Program p, std::string f, std::vector<std::string> a);
  void ingestEntry();
  Program *keep(Program p);
  bool fileHasMod(const std::string &source, const std::string &name) const;
  std::filesystem::path stdlibRoot() const;
  std::vector<std::string> resolveModule(const std::string &name,
                                         const std::string &fromFile);
  void ingestFns(LoadedMod &m, std::vector<FnDecl> &fns, bool fromMod,
                 const std::string &modName, const std::string &atFile);
  void ingestStructs(LoadedMod &m, std::vector<structDecl> &items, bool fromMod,
                     const std::string &modName, const std::string &atFile);
  void ingestTraits(std::vector<TraitDecl> &items, const std::string &atFile);
  void ingestEnums(LoadedMod *m, std::vector<EnumDecl> &items, bool fromMod,
                   const std::string &modName, const std::string &atFile);
  void recordTypeTrait(const std::string &typeName,
                       const std::string &traitName);
  void ingestClasses(LoadedMod *m, std::vector<ClassDecl> &items, bool fromMod,
                     const std::string &modName, const std::string &atFile);
  void ingestImpls(std::vector<ImplDecl> &impls, const std::string &atFile);
  void flattenType(const std::string &name, std::vector<std::string> &stack,
                   std::set<std::string> &done);
  void applyInheritance();
  std::pair<std::string, const FnDecl *>
  lookupMethod(const std::string &typeName, const std::string &name) const;
  void checkTraitImpls();
  void bindTraitSignals();
  void checkConstexprCall(const std::string &name, int line, int col);
  void checkConstexprExpr(const Expr &e);
  void checkConstexprStmt(const Stmt &stmt);
  void checkConstexprFn(const FnDecl &fn);
  void checkConstexprFns();
  void typecheckAll();
  void registerModShells(const ModDecl &block, const std::string &fullName);
  void loadImports(const std::vector<ImportDecl> &imports,
                   const std::string &fromFile, LoadedMod *owner);
  void ingestNestedMods(LoadedMod &parent, std::vector<ModDecl> &mods,
                        const std::string &parentName,
                        const std::string &atFile);
  void ingestLoadedMod(ModDecl &block, const std::string &fullName,
                       const std::string &atFile);
  void ingestModule(Program &p, const std::string &modName, LoadedMod &m,
                    const std::string &atFile);
  void loadModule(const std::string &name, const std::string &fromFile, int line,
                  int col);
  void bindFromImport(const ImportDecl &im, LoadedMod &mod, LoadedMod *owner,
                      const std::string &fromFile);
  void checkDottedExport(const ImportDecl &im, const std::string &fromFile);
  void evalImport(const ImportDecl &im, const std::string &fromFile,
                  LoadedMod *owner);
  void loadImports(Program &p, const std::string &fromFile, LoadedMod *owner);
  const std::string *findModuleBind(const std::string &name) const;
  std::optional<std::string> modulePath(const Expr &e);
  const structDecl *findStruct(const std::string &name) const;
  const EnumDecl *findEnum(const std::string &name) const;
  FnDecl *findLocalFn(const std::string &name);
  Value callModule(const std::string &modName, const std::string &name,
                   const std::vector<Value> &args, int line, int col);
  Binding *findVar(const std::string &name);
  Binding *findLocalBinding(const std::string &name);
  Binding *findGlobalBinding(const std::string &name);
  StructData *selfRec();
  Value *fieldOnSelf(const std::string &name);
  Value resolveValue(const std::string &name, int line, int col);
  Value constructEnum(const EnumDecl &en, const std::string &variant,
                      std::vector<Value> args, int line, int col);
  Value selfObject(int line, int col);
  Value callSuper(const std::string &name, const std::vector<Value> &args,
                  int line, int col);
  size_t indexAt(const Value &idx, int line, int col);
  Value indexGet(const Value &obj, const Value &idx, int line, int col);
  void indexSet(const Value &obj, const Value &idx, Value value, int line,
                int col);
  std::string mapKey(const Value &v, int line, int col);
  Value arrayLen(const Value &obj);
  Value mapLen(const Value &obj);
  std::vector<Value> iterItems(const Value &iter, int line, int col);
  Value callValueMethod(const Value &obj, const std::string &name,
                        const std::vector<Value> &args, int line, int col);
  Value readValueMember(const Value &obj, const std::string &name, int line,
                        int col);
  Value eval(const Expr &e);
  Value callBuiltin(const std::string &module, const std::string &name,
                    const std::vector<Value> &args, int line, int col);
  Value callUser(const FnDecl &fn, const std::vector<Value> &args, int line,
                 int col);
  Value invokeTypeMethod(const Value &obj, const std::string &startType,
                         const std::string &name,
                         const std::vector<Value> &args, int line, int col);
  Value callTypeMethod(const Value &obj, const std::string &name,
                       const std::vector<Value> &args, int line, int col);
  Value callSignal(const std::string &signal, const std::string &name,
                   const std::vector<Value> &args, int line, int col);
  Value call(const std::string &module, const std::string &name,
             const std::vector<Value> &args, int line, int col);
  Flow execBlock(const std::vector<Stmt> &stmts);
  Flow execStmt(const Stmt &stmt);
  Value callNamed(const std::string &name);

  [[noreturn]] void initFail(const std::string &msg) const {
    throw std::runtime_error(locatedError("runtime error", file, 0, 0, msg));
  }

  [[noreturn]] void runtime(const std::string &msg, int line, int col) const {
    throw std::runtime_error(
        locatedError("runtime error", file, line, col, msg));
  }

  [[noreturn]] void runtimeAt(const std::string &atFile, const std::string &msg,
                              int line, int col) const {
    throw std::runtime_error(
        locatedError("runtime error", atFile, line, col, msg));
  }

  [[noreturn]] void constexprFail(int line, int col,
                                  const std::string &msg) const {
    throw std::runtime_error(
        locatedError("constexpr error", file, line, col, msg));
  }
};

std::string readFile(const std::string &path);
bool isNumeric(const Value &v);
double asF64(const Value &v);
bool numericEq(double a, double b);
