#include "eval.h"
#include "parser.h"
#include "lexer.h"

#include <algorithm>
#include <cctype>
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

std::string readFile(const std::string &path);

struct StructData;

struct Value {
  enum class Kind { Void, Bool, Int, String, FnRef, Struct } kind = Kind::Void;
  bool b = false;
  long long i = 0;
  std::string s;
  std::shared_ptr<StructData> rec;

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

  std::string toString() const;
  bool truthy() const;
  bool equals(const Value &other) const;
};

struct StructData {
  std::string name;
  std::vector<std::string> order;
  std::map<std::string, Value> fields;
};

struct LoadedMod {
  std::map<std::string, FnDecl *> fns;
  std::map<std::string, FnDecl *> exports;
  std::map<std::string, structDecl *> structs;
  std::map<std::string, structDecl *> exportStructs;
  std::map<std::string, std::string> modules;
  std::map<std::string, std::string> exportMods;
  std::map<std::string, FnDecl *> fromFns;
};

bool isHostModule(const std::string &name) {
  return name == "checks" || name == "process" || name == "__math" ||
         name == "__str";
}

bool isCrateStdlib(const std::string &name) {
  return name == "math" || name == "str";
}

ModDecl *pickMod(Program &p, const std::string &name) {
  ModDecl *named = nullptr;
  ModDecl *only = nullptr;
  int modCount = 0;
  const bool hasOther = !p.fns.empty() || !p.structs.empty() ||
                        !p.classes.empty() || !p.traits.empty() ||
                        !p.impls.empty() || !p.signals.empty();
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

std::string Value::toString() const {
  switch (kind) {
  case Kind::Void:
    return "";
  case Kind::Bool:
    return b ? "true" : "false";
  case Kind::Int:
    return std::to_string(i);
  case Kind::String:
    return s;
  case Kind::FnRef:
    return s;
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
  }
  return "";
}

bool Value::truthy() const {
  switch (kind) {
  case Kind::Bool:
    return b;
  case Kind::Int:
    return i != 0;
  case Kind::String:
    return !s.empty();
  case Kind::FnRef:
  case Kind::Struct:
    return true;
  default:
    return false;
  }
}

bool Value::equals(const Value &other) const {
  if (kind != other.kind)
    return false;
  switch (kind) {
  case Kind::Void:
    return true;
  case Kind::Bool:
    return b == other.b;
  case Kind::Int:
    return i == other.i;
  case Kind::String:
  case Kind::FnRef:
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
  }
  return false;
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

RunResult failResult(const std::string &message, int exitCode = 1) {
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

  explicit Interpreter(Program p, std::string f, std::vector<std::string> a)
      : program(std::move(p)), file(std::move(f)), argv(std::move(a)) {
    if (argv.empty() && !file.empty())
      argv.push_back(file);
    ingestEntry();
    loadImports(program, file, nullptr);
    applyInheritance();
    checkTraitImpls();
    bindTraitSignals();
    env.emplace_back();
  }

  void ingestEntry() {
    for (const auto &fn : program.fns)
      fns[fn.name] = &fn;
    for (auto &st : program.structs) {
      if (structs.count(st.name))
        initFail("duplicate struct '" + st.name + "'");
      if (fns.count(st.name))
        initFail("struct '" + st.name + "' conflicts with a function");
      structs[st.name] = &st;
      allTypes[st.name] = &st;
      for (const auto &m : st.methods)
        typeMethods[st.name][m.name] = &m;
    }
    ingestClasses(nullptr, program.classes, false, "", file);
    ingestTraits(program.traits, file);
    ingestImpls(program.impls, file);
    for (const auto &sig : program.signals) {
      if (signalArity.count(sig.name))
        initFail("duplicate signal '" + sig.name + "'");
      if (fns.count(sig.name))
        initFail("signal '" + sig.name + "' conflicts with a function");
      if (structs.count(sig.name) || allTypes.count(sig.name))
        initFail("signal '" + sig.name + "' conflicts with a struct");
      signalArity[sig.name] = sig.params.size();
      listeners[sig.name] = {};
    }
    for (auto &m : program.mods) {
      if (fns.count(m.name) || structs.count(m.name) ||
          allTypes.count(m.name) || signalArity.count(m.name))
        initFail("module '" + m.name + "' conflicts with an existing name");
      registerModShells(m, m.name);
    }
    for (auto &m : program.mods) {
      ingestLoadedMod(m, m.name, file);
      moduleBinds[m.name] = m.name;
    }
  }

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

  Program *keep(Program p) {
    extras.push_back(std::make_unique<Program>(std::move(p)));
    return extras.back().get();
  }

  bool fileHasMod(const std::string &source, const std::string &name) const {
    try {
      const auto tokens = tokenize(source, "");
      for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        if (tokens[i].kind == Tok::Module &&
            tokens[i + 1].kind == Tok::Identifier &&
            tokens[i + 1].text == name)
          return true;
      }
    } catch (...) {
    }
    return false;
  }

  std::filesystem::path stdlibRoot() const {
    namespace fs = std::filesystem;
    std::vector<fs::path> starts;
    if (!file.empty()) {
      fs::path p = fs::path(file).parent_path();
      if (p.empty())
        p = ".";
      starts.push_back(p);
    }
    std::error_code ec;
    fs::path cwd = fs::current_path(ec);
    if (!ec)
      starts.push_back(cwd);
    for (fs::path p : starts) {
      while (true) {
        fs::path cand = p / "stdlib";
        if (fs::is_directory(cand, ec) && !ec)
          return cand;
        fs::path parent = p.parent_path();
        if (parent.empty() || parent == p)
          break;
        p = parent;
      }
    }
    return {};
  }

  std::vector<std::string> resolveModule(const std::string &name,
                                         const std::string &fromFile) {
    (void)fromFile;
    namespace fs = std::filesystem;
    fs::path base = fs::path(file).parent_path();
    if (base.empty())
      base = ".";
    std::string stem = name;
    if (stem.size() > 3 && stem.compare(stem.size() - 3, 3, ".rg") == 0)
      stem = stem.substr(0, stem.size() - 3);

    std::vector<std::string> out;
    std::set<std::string> seen;
    auto add = [&](const fs::path &p) {
      std::error_code ec;
      if (!fs::is_regular_file(p, ec) || ec)
        return;
      fs::path key = fs::weakly_canonical(p, ec);
      const std::string id =
          ec ? p.generic_string() : key.generic_string();
      if (!seen.insert(id).second)
        return;
      out.push_back(p.generic_string());
    };
    auto addDir = [&](const fs::path &dir) {
      std::error_code ec;
      if (!fs::is_directory(dir, ec) || ec)
        return;
      std::vector<fs::path> paths;
      for (const auto &entry : fs::directory_iterator(dir, ec)) {
        if (ec)
          break;
        std::error_code fec;
        if (!entry.is_regular_file(fec) || fec)
          continue;
        if (entry.path().extension() == ".rg")
          paths.push_back(entry.path());
      }
      std::sort(paths.begin(), paths.end());
      for (const auto &path : paths)
        add(path);
    };

    if (isCrateStdlib(stem)) {
      fs::path root = stdlibRoot();
      if (!root.empty()) {
        addDir(root / stem);
        if (!out.empty())
          return out;
      }
    }

    std::string dotted = stem;
    for (char &c : dotted) {
      if (c == '.')
        c = static_cast<char>(fs::path::preferred_separator);
    }
    addDir(base / stem);
    if (dotted != stem)
      addDir(base / dotted);

    std::error_code ec;
    if (fs::is_directory(base, ec)) {
      std::vector<fs::path> paths;
      for (const auto &entry : fs::directory_iterator(base, ec)) {
        if (ec)
          break;
        std::error_code fec;
        if (!entry.is_regular_file(fec) || fec)
          continue;
        if (entry.path().extension() == ".rg")
          paths.push_back(entry.path());
      }
      std::sort(paths.begin(), paths.end());
      for (const auto &path : paths) {
        try {
          if (fileHasMod(readFile(path.generic_string()), stem))
            add(path);
        } catch (...) {
        }
      }
    }
    return out;
  }

  void ingestFns(LoadedMod &m, std::vector<FnDecl> &fns, bool fromMod,
                 const std::string &modName, const std::string &atFile) {
    for (auto &fn : fns) {
      fn.module = modName;
      if (m.fns.count(fn.name))
        runtimeAt(atFile,
                  "duplicate export '" + fn.name + "' in module '" +
                      modName + "'",
                  fn.line, 1);
      m.fns[fn.name] = &fn;
      if (!fromMod || fn.isPub)
        m.exports[fn.name] = &fn;
    }
  }

  void ingestStructs(LoadedMod &m, std::vector<structDecl> &items, bool fromMod,
                     const std::string &modName, const std::string &atFile) {
    for (auto &st : items) {
      if (m.structs.count(st.name))
        runtimeAt(atFile,
                  "duplicate export '" + st.name + "' in module '" +
                      modName + "'",
                  st.line, 1);
      m.structs[st.name] = &st;
      allTypes[st.name] = &st;
      for (const auto &method : st.methods)
        typeMethods[st.name][method.name] = &method;
      if (!fromMod || st.isPub) {
        m.exportStructs[st.name] = &st;
        if (structs.count(st.name))
          runtimeAt(atFile, "duplicate struct '" + st.name + "'", st.line, 1);
        if (fns.count(st.name))
          runtimeAt(atFile,
                    "struct '" + st.name + "' conflicts with a function",
                    st.line, 1);
        structs[st.name] = &st;
      }
    }
  }

  void ingestTraits(std::vector<TraitDecl> &items, const std::string &atFile) {
    for (auto &t : items) {
      if (traits.count(t.name))
        runtimeAt(atFile, "duplicate trait '" + t.name + "'", t.line, 1);
      traits[t.name] = &t;
    }
  }

  void recordTypeTrait(const std::string &typeName,
                       const std::string &traitName) {
    auto &list = typeTraits[typeName];
    if (std::find(list.begin(), list.end(), traitName) == list.end())
      list.push_back(traitName);
  }

  void ingestClasses(LoadedMod *m, std::vector<ClassDecl> &items, bool fromMod,
                     const std::string &modName, const std::string &atFile) {
    for (auto &c : items) {
      c.shape.name = c.name;
      c.shape.isPub = c.isPub;
      c.shape.line = c.line;
      c.shape.fields.clear();
      for (const auto &f : c.fields)
        c.shape.fields.push_back(f.name);
      if (m) {
        if (m->structs.count(c.name))
          runtimeAt(atFile,
                    "duplicate export '" + c.name + "' in module '" + modName +
                        "'",
                    c.line, 1);
        m->structs[c.name] = &c.shape;
      }
      if (allTypes.count(c.name))
        runtimeAt(atFile, "duplicate class '" + c.name + "'", c.line, 1);
      if (fns.count(c.name))
        runtimeAt(atFile, "class '" + c.name + "' conflicts with a function",
                  c.line, 1);
      allTypes[c.name] = &c.shape;
      for (const auto &f : c.fields) {
        if (f.hasDefault)
          fieldDefaults[c.name][f.name] = &f.defaultValue;
      }
      if (!c.parent.empty())
        classParents[c.name] = c.parent;
      auto &slot = typeMethods[c.name];
      for (const auto &method : c.methods) {
        if (slot.count(method.name))
          runtimeAt(atFile,
                    "duplicate method '" + method.name + "' on " + c.name,
                    method.line, 1);
        slot[method.name] = &method;
      }
      for (auto &block : c.traitImpls) {
        recordTypeTrait(c.name, block.traitName);
        for (auto &method : block.methods) {
          if (slot.count(method.name))
            runtimeAt(atFile,
                      "duplicate method '" + method.name + "' on " + c.name,
                      method.line, 1);
          slot[method.name] = &method;
        }
      }
      for (const auto &t : c.implTraits)
        recordTypeTrait(c.name, t);
      if (!m || !fromMod || c.isPub) {
        if (m)
          m->exportStructs[c.name] = &c.shape;
        if (structs.count(c.name))
          runtimeAt(atFile, "duplicate class '" + c.name + "'", c.line, 1);
        structs[c.name] = &c.shape;
      }
    }
  }

  void ingestImpls(std::vector<ImplDecl> &impls, const std::string &atFile) {
    for (auto &im : impls) {
      auto &slot = typeMethods[im.typeName];
      const structDecl *st = nullptr;
      auto git = allTypes.find(im.typeName);
      if (git != allTypes.end())
        st = git->second;
      else {
        auto sit = structs.find(im.typeName);
        if (sit != structs.end())
          st = sit->second;
      }
      if (!st)
        runtimeAt(atFile, "undefined struct '" + im.typeName + "'", im.line, 1);
      if (!im.traitName.empty()) {
        if (!traits.count(im.traitName))
          runtimeAt(atFile, "undefined trait '" + im.traitName + "'", im.line,
                    1);
        recordTypeTrait(im.typeName, im.traitName);
      }
      for (auto &method : im.methods) {
        if (st) {
          for (const auto &fld : st->fields) {
            if (fld == method.name)
              runtimeAt(atFile,
                        "method '" + method.name + "' conflicts with field '" +
                            method.name + "' on " + im.typeName,
                        method.line, 1);
          }
        }
        if (slot.count(method.name))
          runtimeAt(atFile,
                    "duplicate method '" + method.name + "' on " + im.typeName,
                    method.line, 1);
        slot[method.name] = &method;
      }
    }
  }

  void flattenType(const std::string &name, std::vector<std::string> &stack,
                   std::set<std::string> &done) {
    if (done.count(name))
      return;
    if (std::find(stack.begin(), stack.end(), name) != stack.end())
      initFail("cycle in class inheritance at '" + name + "'");
    auto pit = classParents.find(name);
    if (pit == classParents.end()) {
      done.insert(name);
      return;
    }
    const std::string &parent = pit->second;
    if (!allTypes.count(parent)) {
      if (typeMethods.count(parent)) {
        done.insert(name);
        return;
      }
      initFail("class '" + name + "' extends unknown type '" + parent + "'");
    }
    stack.push_back(name);
    flattenType(parent, stack, done);
    stack.pop_back();
    structDecl *child = const_cast<structDecl *>(allTypes[name]);
    const structDecl *parentDef = allTypes[parent];
    std::vector<std::string> fields = parentDef->fields;
    for (const auto &f : child->fields) {
      if (std::find(fields.begin(), fields.end(), f) == fields.end())
        fields.push_back(f);
    }
    child->fields = std::move(fields);
    std::map<std::string, const Expr *> merged;
    auto parentDefs = fieldDefaults.find(parent);
    if (parentDefs != fieldDefaults.end())
      merged = parentDefs->second;
    auto childDefs = fieldDefaults.find(name);
    if (childDefs != fieldDefaults.end()) {
      for (const auto &kv : childDefs->second)
        merged[kv.first] = kv.second;
    }
    fieldDefaults[name] = std::move(merged);
    done.insert(name);
  }

  void applyInheritance() {
    std::set<std::string> done;
    std::vector<std::string> stack;
    std::vector<std::string> names;
    names.reserve(classParents.size());
    for (const auto &kv : classParents)
      names.push_back(kv.first);
    for (const auto &name : names)
      flattenType(name, stack, done);
  }

  std::pair<std::string, const FnDecl *>
  lookupMethod(const std::string &typeName, const std::string &name) const {
    std::string current = typeName;
    std::set<std::string> seen;
    while (!current.empty()) {
      if (!seen.insert(current).second)
        break;
      auto typeIt = typeMethods.find(current);
      if (typeIt != typeMethods.end()) {
        auto mit = typeIt->second.find(name);
        if (mit != typeIt->second.end())
          return {current, mit->second};
      }
      auto pit = classParents.find(current);
      if (pit == classParents.end())
        break;
      current = pit->second;
    }
    return {"", nullptr};
  }

  void checkTraitImpls() {
    for (const auto &kv : typeTraits) {
      for (const auto &traitName : kv.second) {
        auto tit = traits.find(traitName);
        if (tit == traits.end())
          initFail("undefined trait '" + traitName + "'");
        for (const auto &m : tit->second->methods) {
          if (!lookupMethod(kv.first, m.name).second)
            initFail("type '" + kv.first + "' is missing '" + m.name +
                     "' for trait '" + traitName + "'");
        }
      }
    }
  }

  void bindTraitSignals() {
    for (const auto &kv : typeTraits) {
      for (const auto &traitName : kv.second) {
        auto tit = traits.find(traitName);
        if (tit == traits.end())
          continue;
        for (const auto &sig : tit->second->signals) {
          if (signalArity.count(sig.name))
            continue;
          if (fns.count(sig.name) || structs.count(sig.name) ||
              allTypes.count(sig.name))
            continue;
          signalArity[sig.name] = sig.params.size();
          listeners[sig.name] = {};
        }
      }
    }
  }

  void registerModShells(const ModDecl &block, const std::string &fullName) {
    if (!loaded.count(fullName))
      loaded[fullName] = LoadedMod{};
    for (const auto &child : block.mods)
      registerModShells(child, fullName + "." + child.name);
  }

  void loadImports(const std::vector<ImportDecl> &imports,
                   const std::string &fromFile, LoadedMod *owner) {
    for (const auto &im : imports)
      evalImport(im, fromFile, owner);
  }

  void ingestNestedMods(LoadedMod &parent, std::vector<ModDecl> &mods,
                        const std::string &parentName,
                        const std::string &atFile) {
    for (auto &child : mods) {
      const std::string full = parentName + "." + child.name;
      ingestLoadedMod(child, full, atFile);
      parent.modules[child.name] = full;
      if (child.isPub)
        parent.exportMods[child.name] = full;
    }
  }

  void ingestLoadedMod(ModDecl &block, const std::string &fullName,
                       const std::string &atFile) {
    LoadedMod dest;
    if (loaded.count(fullName))
      dest = std::move(loaded[fullName]);
    ingestFns(dest, block.fns, true, fullName, atFile);
    ingestStructs(dest, block.structs, true, fullName, atFile);
    ingestClasses(&dest, block.classes, true, fullName, atFile);
    ingestTraits(block.traits, atFile);
    ingestImpls(block.impls, atFile);
    ingestNestedMods(dest, block.mods, fullName, atFile);
    loadImports(block.imports, atFile, &dest);
    loaded[fullName] = std::move(dest);
  }

  void ingestModule(Program &p, const std::string &modName, LoadedMod &m,
                    const std::string &atFile) {
    ModDecl *block = pickMod(p, modName);
    const bool fromMod = block != nullptr;
    if (block) {
      ingestFns(m, block->fns, fromMod, modName, atFile);
      ingestStructs(m, block->structs, fromMod, modName, atFile);
      ingestClasses(&m, block->classes, fromMod, modName, atFile);
      ingestTraits(block->traits, atFile);
      ingestImpls(block->impls, atFile);
      ingestNestedMods(m, block->mods, modName, atFile);
      loadImports(block->imports, atFile, &m);
    } else {
      ingestFns(m, p.fns, false, modName, atFile);
      ingestStructs(m, p.structs, false, modName, atFile);
      ingestClasses(&m, p.classes, false, modName, atFile);
      ingestTraits(p.traits, atFile);
      ingestImpls(p.impls, atFile);
      loadImports(p.imports, atFile, &m);
    }
  }

  void loadModule(const std::string &name, const std::string &fromFile, int line,
                  int col) {
    if (loaded.count(name) || isHostModule(name))
      return;
    for (const auto &cur : loading) {
      if (cur == name)
        runtimeAt(fromFile, "cyclic import of module '" + name + "'", line,
                  col);
    }
    const auto dot = name.rfind('.');
    if (dot != std::string::npos) {
      const std::string parent = name.substr(0, dot);
      if (!loaded.count(parent) && !isHostModule(parent) &&
          !resolveModule(parent, fromFile).empty())
        loadModule(parent, fromFile, line, col);
      if (loaded.count(name))
        return;
    }
    const std::vector<std::string> files = resolveModule(name, fromFile);
    if (files.empty())
      runtimeAt(fromFile,
                "module '" + name + "' not found (tried " + name +
                    "/ or mod " + name + ")",
                line, col);

    loading.push_back(name);
    LoadedMod m;
    try {
      for (const auto &path : files) {
        Program *kept = keep(parseSource(readFile(path), path));
        ingestModule(*kept, name, m, path);
      }
    } catch (...) {
      loading.pop_back();
      throw;
    }
    loading.pop_back();
    loaded[name] = std::move(m);
  }

  void bindFromImport(const ImportDecl &im, LoadedMod &mod, LoadedMod *owner,
                      const std::string &fromFile) {
    if (im.path.size() != 2)
      runtimeAt(fromFile,
                "nested from-imports longer than 2 segments are not supported",
                im.line, im.col);
    const std::string &item = im.path[1];
    const std::string alias = im.alias.empty() ? item : im.alias;
    if (FnDecl *fn = mod.exports.count(item) ? mod.exports[item] : nullptr) {
      if (owner) {
        if (owner->fromFns.count(alias) || owner->fns.count(alias))
          runtimeAt(fromFile, "duplicate import '" + alias + "'", im.line,
                    im.col);
        owner->fromFns[alias] = fn;
      } else {
        if (fns.count(alias))
          runtimeAt(fromFile, "duplicate import '" + alias + "'", im.line,
                    im.col);
        fns[alias] = fn;
      }
      return;
    }
    if (structDecl *st =
            mod.exportStructs.count(item) ? mod.exportStructs[item] : nullptr) {
      if (owner) {
        owner->structs[alias] = st;
        owner->exportStructs[alias] = st;
      } else {
        if (structs.count(alias) && structs[alias] != st)
          runtimeAt(fromFile, "duplicate struct '" + alias + "'", im.line,
                    im.col);
        structs[alias] = st;
      }
      return;
    }
    if (mod.exportMods.count(item)) {
      const std::string &full = mod.exportMods[item];
      if (owner)
        owner->modules[alias] = full;
      else
        moduleBinds[alias] = full;
      return;
    }
    runtimeAt(fromFile,
              "module '" + im.path[0] + "' has no export '" + item + "'",
              im.line, im.col);
  }

  void checkDottedExport(const ImportDecl &im, const std::string &fromFile) {
    if (im.path.size() < 2)
      return;
    std::string acc = im.path[0];
    for (size_t i = 1; i < im.path.size(); ++i) {
      auto pit = loaded.find(acc);
      if (pit == loaded.end())
        return;
      const std::string &seg = im.path[i];
      if (pit->second.modules.count(seg)) {
        if (!pit->second.exportMods.count(seg))
          runtimeAt(fromFile,
                    "module '" + acc + "' has no export '" + seg + "'",
                    im.line, im.col);
      } else {
        return;
      }
      acc += ".";
      acc += seg;
    }
  }

  void evalImport(const ImportDecl &im, const std::string &fromFile,
                  LoadedMod *owner) {
    if (im.path.empty())
      runtimeAt(fromFile, "empty import", im.line, im.col);
    if (isHostModule(im.path[0]))
      return;
    if (im.isFrom) {
      loadModule(im.path[0], fromFile, im.line, im.col);
      auto it = loaded.find(im.path[0]);
      if (it == loaded.end())
        return;
      bindFromImport(im, it->second, owner, fromFile);
      return;
    }
    const std::string full = [&]() {
      std::string s = im.path[0];
      for (size_t i = 1; i < im.path.size(); ++i) {
        s += ".";
        s += im.path[i];
      }
      return s;
    }();
    loadModule(full, fromFile, im.line, im.col);
    checkDottedExport(im, fromFile);
    const std::string bind =
        im.alias.empty() ? im.path.back() : im.alias;
    if (owner)
      owner->modules[bind] = full;
    else
      moduleBinds[bind] = full;
  }

  void loadImports(Program &p, const std::string &fromFile, LoadedMod *owner) {
    loadImports(p.imports, fromFile, owner);
  }

  const std::string *findModuleBind(const std::string &name) const {
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

  std::optional<std::string> modulePath(const Expr &e) {
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

  const structDecl *findStruct(const std::string &name) const {
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

  FnDecl *findLocalFn(const std::string &name) {
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

  Value callModule(const std::string &modName, const std::string &name,
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

  Binding *findVar(const std::string &name) {
    for (int i = static_cast<int>(env.size()) - 1; i >= 0; --i) {
      auto it = env[static_cast<size_t>(i)].find(name);
      if (it != env[static_cast<size_t>(i)].end())
        return &it->second;
    }
    return nullptr;
  }

  Binding *findLocalBinding(const std::string &name) {
    if (env.size() < 2)
      return nullptr;
    auto it = env.back().find(name);
    if (it != env.back().end())
      return &it->second;
    return nullptr;
  }

  Binding *findGlobalBinding(const std::string &name) {
    if (env.empty())
      return nullptr;
    auto it = env[0].find(name);
    if (it != env[0].end())
      return &it->second;
    return nullptr;
  }

  StructData *selfRec() {
    Binding *self = findLocalBinding("self");
    if (!self || self->value.kind != Value::Kind::Struct || !self->value.rec)
      return nullptr;
    return self->value.rec.get();
  }

  Value *fieldOnSelf(const std::string &name) {
    StructData *rec = selfRec();
    if (!rec)
      return nullptr;
    auto it = rec->fields.find(name);
    if (it == rec->fields.end())
      return nullptr;
    return &it->second;
  }

  Value resolveValue(const std::string &name, int line, int col) {
    if (Binding *local = findLocalBinding(name))
      return local->value;
    if (Value *field = fieldOnSelf(name))
      return *field;
    if (Binding *global = findGlobalBinding(name))
      return global->value;
    if (findLocalFn(name))
      return Value::makeFnRef(name);
    runtime("undefined variable '" + name + "'", line, col);
  }

  Value selfObject(int line, int col) {
    Binding *self = findVar("self");
    if (!self || self->value.kind != Value::Kind::Struct || !self->value.rec)
      runtime("super requires self", line, col);
    return self->value;
  }

  Value callSuper(const std::string &name, const std::vector<Value> &args,
                  int line, int col) {
    if (superType.empty())
      runtime(
          "super is only valid in a method of a class that extends another",
          line, col);
    return invokeTypeMethod(selfObject(line, col), superType, name, args, line,
                            col);
  }

  Value eval(const Expr &e) {
    switch (e.kind) {
    case Expr::Kind::Int:
      return Value::makeInt(e.number);
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
        if (v.kind != Value::Kind::Int)
          runtime("unary '-' expects Int", e.line, e.col);
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
      if (a.kind != Value::Kind::Int || b.kind != Value::Kind::Int)
        runtime("operands must be Int", e.line, e.col);
      if (e.text == "+")
        return Value::makeInt(a.i + b.i);
      if (e.text == "-")
        return Value::makeInt(a.i - b.i);
      if (e.text == "*")
        return Value::makeInt(a.i * b.i);
      if (e.text == "/") {
        if (b.i == 0)
          runtime("division by zero", e.line, e.col);
        return Value::makeInt(a.i / b.i);
      }
      if (e.text == "%") {
        if (b.i == 0)
          runtime("modulo by zero", e.line, e.col);
        return Value::makeInt(a.i % b.i);
      }
      if (e.text == "<")
        return Value::makeBool(a.i < b.i);
      if (e.text == ">")
        return Value::makeBool(a.i > b.i);
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
            findGlobalBinding(recv.text)) {
          Value obj = resolveValue(recv.text, recv.line, recv.col);
          if (obj.kind == Value::Kind::Struct && obj.rec)
            return callTypeMethod(obj, e.text, args, e.line, e.col);
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
      runtime("cannot call method '" + e.text + "' on " + obj.toString(),
              e.line, e.col);
    }
    case Expr::Kind::Member: {
      Value obj;
      if (e.kids[0].kind == Expr::Kind::Var && e.kids[0].text == "super")
        obj = selfObject(e.line, e.col);
      else
        obj = eval(e.kids[0]);
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
    }
    return Value::makeVoid();
  }

  Value callBuiltin(const std::string &module, const std::string &name,
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
        if (args[0].kind != Value::Kind::Int ||
            args[1].kind != Value::Kind::Int)
          runtime("checks.eq expects Int, Int", line, col);
        if (args[0].i != args[1].i)
          runtime("assertion failed", line, col);
        return Value::makeVoid();
      }
      if (name == "neq") {
        need(2);
        if (args[0].kind != Value::Kind::Int ||
            args[1].kind != Value::Kind::Int)
          runtime("checks.neq expects Int, Int", line, col);
        if (args[0].i == args[1].i)
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

  Value callUser(const FnDecl &fn, const std::vector<Value> &args, int line,
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

  Value invokeTypeMethod(const Value &obj, const std::string &startType,
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

  Value callTypeMethod(const Value &obj, const std::string &name,
                       const std::vector<Value> &args, int line, int col) {
    return invokeTypeMethod(obj, obj.rec->name, name, args, line, col);
  }

  Value callSignal(const std::string &signal, const std::string &name,
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

  Value call(const std::string &module, const std::string &name,
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

  Flow execBlock(const std::vector<Stmt> &stmts) {
    for (const auto &stmt : stmts) {
      Flow f = execStmt(stmt);
      if (f.kind != Flow::Kind::Next)
        return f;
    }
    return Flow::next();
  }

  Flow execStmt(const Stmt &stmt) {
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

  Value callNamed(const std::string &name) {
    auto it = fns.find(name);
    if (it == fns.end())
      throw std::runtime_error(
          locatedError("runtime error", file, 0, 0,
                       "unknown function '" + name + "'"));
    return callUser(*it->second, {}, it->second->line, 1);
  }
};

std::string readFile(const std::string &path) {
  std::ifstream in(path);
  if (!in)
    throw std::runtime_error("failed to read " + path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

} // namespace

RunResult runFile(const std::string &path, const std::vector<std::string> &argv) {
  try {
    Program program = parseSource(readFile(path), path);
    Interpreter interp(std::move(program), path, argv);
    Value ret = Value::makeVoid();
    if (interp.fns.count("main"))
      ret = interp.callNamed("main");
    RunResult result;
    result.out = interp.out;
    if (ret.kind == Value::Kind::Int)
      result.exitCode = static_cast<int>(ret.i);
    return result;
  } catch (const std::exception &ex) {
    return failResult(ex.what());
  }
}

RunResult testFile(const std::string &path) {
  try {
    Program program = parseSource(readFile(path), path);
    std::vector<std::string> tests;
    for (const auto &fn : program.fns) {
      if (fn.isTest)
        tests.push_back(fn.name);
    }
    if (tests.empty()) {
      RunResult result;
      result.message = "no @test functions";
      return result;
    }

    Interpreter interp(std::move(program), path, {path});
    int failed = 0;
    for (const auto &name : tests) {
      try {
        interp.callNamed(name);
        interp.out += "ok " + name + "\n";
      } catch (const std::exception &ex) {
        ++failed;
        interp.out += "FAIL " + name + ": " + ex.what() + "\n";
      }
    }
    const int total = static_cast<int>(tests.size());
    const int passed = total - failed;
    const std::string summary =
        std::to_string(passed) + "/" + std::to_string(total) + " tests passed";
    interp.out += summary + "\n";
    RunResult result;
    result.ok = failed == 0;
    result.out = interp.out;
    result.message = summary;
    result.exitCode = failed == 0 ? 0 : 1;
    return result;
  } catch (const std::exception &ex) {
    return failResult(ex.what());
  }
}

namespace fs = std::filesystem;

static std::string trimCopy(std::string s) {
  auto a = s.find_first_not_of(" \t\r");
  if (a == std::string::npos)
    return "";
  auto b = s.find_last_not_of(" \t\r");
  return s.substr(a, b - a + 1);
}

static std::string readExpect(const std::string &source) {
  std::istringstream in(source);
  std::string line;
  while (std::getline(in, line)) {
    line = trimCopy(line);
    const std::string prefixes[] = {"# expect:", "// expect:", "/// expect:"};
    for (const auto &p : prefixes) {
      if (line.size() >= p.size() && line.compare(0, p.size(), p) == 0)
        return trimCopy(line.substr(p.size()));
    }
  }
  return "";
}

static std::vector<fs::path> listRgFiles(const fs::path &dir) {
  std::vector<fs::path> out;
  std::error_code ec;
  if (!fs::is_directory(dir, ec))
    return out;
  for (const auto &entry : fs::directory_iterator(dir, ec)) {
    if (ec)
      break;
    std::error_code fec;
    if (!entry.is_regular_file(fec) || fec)
      continue;
    if (entry.path().extension() == ".rg")
      out.push_back(entry.path());
  }
  std::sort(out.begin(), out.end());
  return out;
}

RunResult testSuite(const std::string &root) {
  std::error_code ec;
  if (!fs::is_directory(root, ec))
    return failResult("no test directory " + root);

  int failed = 0;
  int total = 0;
  std::string out;

  auto checkPass = [&](const fs::path &path) {
    const std::string name = path.generic_string();
    try {
      Program program = parseSource(readFile(name), name);
      bool hasMain = false;
      for (const auto &fn : program.fns) {
        if (fn.name == "main") {
          hasMain = true;
          break;
        }
      }
      if (!hasMain)
        return;
    } catch (...) {
    }
    ++total;
    RunResult r = runFile(name);
    if (r.ok && r.exitCode == 0) {
      out += "ok   " + name + "\n";
      return;
    }
    ++failed;
    std::string got = r.message.empty() ? r.out : r.message;
    if (got.empty())
      got = "exit " + std::to_string(r.exitCode);
    out += "FAIL " + name + ": expected pass, got " + got + "\n";
  };

  auto checkFail = [&](const fs::path &path) {
    const std::string name = path.generic_string();
    std::string source;
    try {
      source = readFile(name);
    } catch (const std::exception &ex) {
      ++total;
      ++failed;
      out += "FAIL " + name + ": " + ex.what() + "\n";
      return;
    }
    const std::string expect = readExpect(source);
    if (expect.empty())
      return;
    ++total;
    RunResult r = runFile(name);
    const bool didFail = !r.ok || r.exitCode != 0;
    const std::string got = r.message.empty() ? r.out : r.message;
    if (!didFail) {
      ++failed;
      out += "FAIL " + name + ": expected error, program succeeded\n";
      return;
    }
    if (!expect.empty() && got.find(expect) == std::string::npos) {
      ++failed;
      out += "FAIL " + name + ": expected '" + expect + "', got " + got + "\n";
      return;
    }
    out += "ok   " + name + "\n";
  };

  for (const auto &p : listRgFiles(fs::path(root) / "pass"))
    checkPass(p);
  for (const auto &p : listRgFiles(fs::path(root) / "fail"))
    checkFail(p);

  if (total == 0)
    return failResult("no .rg files in " + root + "/pass or " + root + "/fail");

  const int passed = total - failed;
  const std::string summary = std::to_string(passed) + "/" +
                              std::to_string(total) + " file tests passed";
  out += summary + "\n";
  RunResult result;
  result.ok = failed == 0;
  result.out = out;
  result.message = summary;
  result.exitCode = failed == 0 ? 0 : 1;
  return result;
}

RunResult testLanguage() {
  std::error_code ec;
  const bool hasUnit = fs::is_regular_file("examples/tests.rg", ec);
  const bool hasSuite = fs::is_directory("tests", ec);
  if (!hasUnit && !hasSuite)
    return failResult("no tests found (examples/tests.rg or tests/)");

  RunResult result;
  result.ok = true;
  if (hasUnit) {
    RunResult unit = testFile("examples/tests.rg");
    result.out += unit.out;
    if (!unit.ok)
      result.ok = false;
  }
  if (hasSuite) {
    RunResult files = testSuite("tests");
    result.out += files.out;
    if (!files.ok)
      result.ok = false;
  }
  result.exitCode = result.ok ? 0 : 1;
  if (!result.ok)
    result.message = "tests failed";
  return result;
}
