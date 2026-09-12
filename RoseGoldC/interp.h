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

inline std::string typeHead(const std::string &ty) {
  const auto p = ty.find('[');
  return p == std::string::npos ? ty : ty.substr(0, p);
}

inline std::vector<std::string> typeArgList(const std::string &ty) {
  const auto p = ty.find('[');
  if (p == std::string::npos || ty.empty() || ty.back() != ']')
    return {};
  const std::string inner = ty.substr(p + 1, ty.size() - p - 2);
  std::vector<std::string> out;
  std::string cur;
  int depth = 0;
  auto flush = [&]() {
    const auto a = cur.find_first_not_of(' ');
    const auto b = cur.find_last_not_of(' ');
    if (a != std::string::npos)
      out.push_back(cur.substr(a, b - a + 1));
    cur.clear();
  };
  for (char c : inner) {
    if (c == '[')
      ++depth;
    else if (c == ']')
      --depth;
    if (c == ',' && depth == 0) {
      flush();
      continue;
    }
    cur += c;
  }
  if (!cur.empty())
    flush();
  return out;
}

inline std::string typeApply(const std::string &name,
                             const std::vector<std::string> &args) {
  if (args.empty())
    return name;
  std::string out = name + "[";
  for (size_t i = 0; i < args.size(); ++i) {
    if (i)
      out += ", ";
    out += args[i];
  }
  out += "]";
  return out;
}

inline std::string substType(const std::string &ty,
                             const std::map<std::string, std::string> &env) {
  if (ty.empty())
    return ty;
  const auto args = typeArgList(ty);
  std::string head = typeHead(ty);
  if (args.empty()) {
    auto it = env.find(head);
    return it == env.end() ? head : it->second;
  }
  std::vector<std::string> out;
  out.reserve(args.size());
  for (const auto &a : args)
    out.push_back(substType(a, env));
  auto it = env.find(head);
  if (it != env.end())
    head = it->second;
  return typeApply(head, out);
}

inline std::map<std::string, std::string>
typeEnvFrom(const std::vector<TypeParam> &params,
            const std::vector<std::string> &args) {
  std::map<std::string, std::string> env;
  if (args.size() == params.size()) {
    for (size_t i = 0; i < params.size(); ++i)
      env[params[i].name] = args[i];
  } else {
    for (const auto &p : params)
      env[p.name] = p.name;
  }
  return env;
}

inline std::string selfApplied(const std::string &name,
                               const std::vector<TypeParam> &params) {
  if (params.empty())
    return name;
  std::vector<std::string> args;
  args.reserve(params.size());
  for (const auto &p : params)
    args.push_back(p.name);
  return typeApply(name, args);
}

inline std::string appliedParent(
    const std::map<std::string, std::string> &classParents,
    const std::map<std::string, const structDecl *> &allTypes,
    const std::string &applied) {
  auto pit = classParents.find(typeHead(applied));
  if (pit == classParents.end())
    return "";
  std::vector<TypeParam> params;
  auto tit = allTypes.find(typeHead(applied));
  if (tit != allTypes.end() && tit->second)
    params = tit->second->typeParams;
  return substType(pit->second, typeEnvFrom(params, typeArgList(applied)));
}

struct TraitImplInfo {
  std::vector<TypeParam> typeParams;
  std::string typeName;
  std::string traitName;
  int line = 1;
};

struct StructData;
struct MapData;
struct ClosureData;

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
    Range,
    EnumType,
    Enum,
    SignalRef
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
  std::shared_ptr<ClosureData> clo;

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
  static Value makeClosure(std::shared_ptr<ClosureData> data) {
    Value x;
    x.kind = Kind::FnRef;
    x.s = "<fn>";
    x.clo = std::move(data);
    return x;
  }
  static Value makeSignalRef(std::string name,
                             std::shared_ptr<StructData> owner) {
    Value x;
    x.kind = Kind::SignalRef;
    x.s = std::move(name);
    x.rec = std::move(owner);
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
  static Value makeRange(long long start, long long end, bool inclusive) {
    Value x;
    x.kind = Kind::Range;
    x.i = start;
    x.b = inclusive;
    x.payload.push_back(makeInt(end));
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
  std::map<std::string, std::vector<Value>> listeners;
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
         name == "__str" || name == "__io" || name == "__uuid" ||
         name == "__time" || name == "__path" || name == "__json" ||
         name == "__ui";
}

inline bool isStdlibChild(const std::string &name) {
  return name == "math" || name == "str" || name == "io" || name == "vec" ||
         name == "time" || name == "path" || name == "json" || name == "ui";
}

inline bool isCrateStdlib(const std::string &name) {
  if (name == "std" || isStdlibChild(name))
    return true;
  return name.size() > 4 && name.compare(0, 4, "std.") == 0;
}

inline std::string canonicalStdlibName(const std::string &name) {
  if (isStdlibChild(name))
    return "std." + name;
  return name;
}

struct StdlibExport {
  std::string crate;
  std::string kind;
};

std::filesystem::path findStdlibRoot(const std::string &fromFile);
const std::map<std::string, std::vector<StdlibExport>> &
stdlibExportIndex(const std::string &fromFile);
const StdlibExport *lookupStdlibExport(const std::string &name,
                                       const std::string &fromFile);
std::string stdlibImportHint(const std::string &name,
                             const std::string &fromFile);

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

struct ThrowEscape {
  Value value;
  int line = 1;
  int col = 1;
};

struct Flow {
  enum class Kind { Next, Return, Break, Continue, Throw } kind = Kind::Next;
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

  static Flow thr(Value v) {
    Flow f;
    f.kind = Kind::Throw;
    f.value = std::move(v);
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

struct ClosureData {
  std::shared_ptr<FnDecl> fn;
  std::map<std::string, Binding> caps;
};

struct DeferredEmit {
  std::string signal;
  std::shared_ptr<StructData> rec;
  std::vector<Value> args;
  int line = 1;
  int col = 1;
};

void uiHostReset();

struct Interpreter {
  Program program;
  std::map<std::string, const FnDecl *> fns;
  std::map<std::string, const structDecl *> structs;
  std::map<std::string, const structDecl *> allTypes;
  std::map<std::string, const TraitDecl *> traits;
  std::map<std::string, const EnumDecl *> enums;
  std::map<std::string, std::string> classParents;
  std::map<std::string, bool> classAbstract;
  std::map<std::string, bool> classFinal;
  std::map<std::string, std::map<std::string, Vis>> fieldAccess;
  std::map<std::string, std::map<std::string, const Expr *>> fieldDefaults;
  std::map<std::string, std::vector<std::string>> typeTraits;
  std::vector<TraitImplInfo> traitImpls;
  std::map<std::string, std::map<std::string, const FnDecl *>> typeMethods;
  std::map<std::string, std::map<std::string, std::size_t>> typeSignals;
  std::map<std::string, std::size_t> signalArity;
  std::map<std::string, std::vector<Value>> listeners;
  std::vector<DeferredEmit> deferred;
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
  std::vector<Diagnostic> diagnostics;

  explicit Interpreter(Program p, std::string f, std::vector<std::string> a,
                       bool failFast = true);
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
  void recordTraitImpl(const std::string &typeName,
                       const std::string &traitName,
                       const std::vector<TypeParam> &params, int line);
  const TraitDecl *findTrait(const std::string &name) const;
  void registerTypeSignals(const std::string &typeName,
                           const std::vector<SignalDecl> &sigs,
                           const std::string &atFile);
  std::optional<std::size_t> lookupTypeSignal(const std::string &typeName,
                                              const std::string &name) const;
  void initInstanceSignals(StructData &data) const;
  void ingestClasses(LoadedMod *m, std::vector<ClassDecl> &items, bool fromMod,
                     const std::string &modName, const std::string &atFile);
  void ingestImpls(std::vector<ImplDecl> &impls, const std::string &atFile);
  void flattenType(const std::string &name, std::vector<std::string> &stack,
                   std::set<std::string> &done);
  void applyInheritance();
  std::pair<std::string, const FnDecl *>
  lookupMethod(const std::string &typeName, const std::string &name) const;
  std::pair<std::string, Vis> lookupField(const std::string &typeName,
                                          const std::string &name) const;
  void recordFieldAccess(const std::string &typeName,
                         const std::vector<std::string> &fields,
                         const std::vector<char> &vis);
  void checkTraitImpls();
  void checkAbstractFinal();
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
  std::string crateNameOfFile(const std::string &path) const;
  std::filesystem::path crateDir(const std::string &name) const;
  void ingestCrateEntry(const std::string &crate);
  void attachCrateChildren(const std::string &parent);
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
  bool isDataType(const std::string &name) const;
  const EnumDecl *findEnum(const std::string &name) const;
  FnDecl *findLocalFn(const std::string &name);
  FnDecl *findUfcs(const std::string &name);
  Value callUfcs(const FnDecl &fn, const Value &obj,
                 const std::vector<Value> &args, int line, int col);
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
  Value applyBinop(const std::string &op, const Value &a, const Value &b,
                   int line, int col);
  Value applyAssignOp(const std::string &op, const Value &old, const Value &rhs,
                      int line, int col);
  Value eval(const Expr &e);
  Value callBuiltin(const std::string &module, const std::string &name,
                    const std::vector<Value> &args, int line, int col);
  Value callUser(const FnDecl &fn, const std::vector<Value> &args, int line,
                 int col, const std::map<std::string, Binding> *caps = nullptr);
  Value callFnValue(const Value &fn, const std::vector<Value> &args, int line,
                    int col);
  Value invokeTypeMethod(const Value &obj, const std::string &startType,
                         const std::string &name,
                         const std::vector<Value> &args, int line, int col);
  Value callTypeMethod(const Value &obj, const std::string &name,
                       const std::vector<Value> &args, int line, int col);
  Value callSignal(const std::string &signal, const std::string &name,
                   const std::vector<Value> &args, int line, int col);
  Value callSignalList(const std::string &signal, std::size_t arity,
                       std::vector<Value> &list,
                       std::shared_ptr<StructData> rec, const std::string &name,
                       const std::vector<Value> &args, int line, int col);
  Value dispatchSignal(const Value &sig, const std::string &name,
                       const std::vector<Value> &args, int line, int col);
  void flushDeferred();
  Value evalLambda(const Expr &e);
  Value call(const std::string &module, const std::string &name,
             const std::vector<Value> &args, int line, int col);
  Flow execBlock(const std::vector<Stmt> &stmts);
  Flow execStmt(const Stmt &stmt);
  Value callNamed(const std::string &name);

  void recordDiag(const std::string &kind, const std::string &atFile, int line,
                  int col, const std::string &msg) {
    Diagnostic d;
    d.file = atFile.empty() ? file : atFile;
    d.line = line > 0 ? line : 1;
    d.col = col > 0 ? col : 1;
    d.severity = "error";
    d.message = msg;
    d.kind = kind;
    for (const auto &prev : diagnostics) {
      if (prev.file == d.file && prev.line == d.line && prev.col == d.col &&
          prev.message == d.message)
        return;
    }
    diagnostics.push_back(std::move(d));
  }

  void initFail(const std::string &msg) {
    recordDiag("runtime error", file, 1, 1, msg);
  }

  void loadFail(const std::string &atFile, const std::string &msg, int line,
                int col) {
    recordDiag("runtime error", atFile, line, col, msg);
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

  void constexprFail(int line, int col, const std::string &msg) {
    recordDiag("constexpr error", file, line, col, msg);
  }

  ~Interpreter() { uiHostReset(); }
};

void uiHostReset();
Value uiHostCall(Interpreter &I, const std::string &name,
                 const std::vector<Value> &args, int line, int col);

std::string readFile(const std::string &path);
bool isNumeric(const Value &v);
double asF64(const Value &v);
bool numericEq(double a, double b);
