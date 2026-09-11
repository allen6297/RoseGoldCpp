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

Interpreter::Interpreter(Program p, std::string f, std::vector<std::string> a)
    : program(std::move(p)), file(std::move(f)), argv(std::move(a)) {
  if (argv.empty() && !file.empty())
    argv.push_back(file);
  ingestEntry();
  loadImports(program, file, nullptr);
  applyInheritance();
  checkTraitImpls();
  bindTraitSignals();
  checkConstexprFns();
  typecheckAll();
  env.emplace_back();
}

void Interpreter::ingestEntry() {
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
  ingestEnums(nullptr, program.enums, false, "", file);
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
        allTypes.count(m.name) || enums.count(m.name) ||
        signalArity.count(m.name))
      initFail("module '" + m.name + "' conflicts with an existing name");
    registerModShells(m, m.name);
  }
  for (auto &m : program.mods) {
    ingestLoadedMod(m, m.name, file);
    moduleBinds[m.name] = m.name;
  }
}

Program *Interpreter::keep(Program p) {
  extras.push_back(std::make_unique<Program>(std::move(p)));
  return extras.back().get();
}

bool Interpreter::fileHasMod(const std::string &source, const std::string &name) const {
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

std::filesystem::path Interpreter::stdlibRoot() const {
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
      fs::path cand = p / "builtin" / "std";
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

std::vector<std::string> Interpreter::resolveModule(const std::string &name,
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

void Interpreter::ingestFns(LoadedMod &m, std::vector<FnDecl> &fns, bool fromMod,
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

void Interpreter::ingestStructs(LoadedMod &m, std::vector<structDecl> &items, bool fromMod,
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

void Interpreter::ingestTraits(std::vector<TraitDecl> &items, const std::string &atFile) {
  for (auto &t : items) {
    if (traits.count(t.name))
      runtimeAt(atFile, "duplicate trait '" + t.name + "'", t.line, 1);
    traits[t.name] = &t;
  }
}

void Interpreter::ingestEnums(LoadedMod *m, std::vector<EnumDecl> &items, bool fromMod,
                 const std::string &modName, const std::string &atFile) {
  for (auto &e : items) {
    if (m) {
      if (m->enums.count(e.name))
        runtimeAt(atFile,
                  "duplicate export '" + e.name + "' in module '" + modName +
                      "'",
                  e.line, 1);
      m->enums[e.name] = &e;
    }
    if (fns.count(e.name))
      runtimeAt(atFile, "enum '" + e.name + "' conflicts with a function",
                e.line, 1);
    if (structs.count(e.name) || allTypes.count(e.name))
      runtimeAt(atFile, "enum '" + e.name + "' conflicts with a type", e.line,
                1);
    if (!m || !fromMod || e.isPub) {
      if (m)
        m->exportEnums[e.name] = &e;
      if (enums.count(e.name))
        runtimeAt(atFile, "duplicate enum '" + e.name + "'", e.line, 1);
      enums[e.name] = &e;
    }
  }
}

void Interpreter::recordTypeTrait(const std::string &typeName,
                     const std::string &traitName) {
  auto &list = typeTraits[typeName];
  if (std::find(list.begin(), list.end(), traitName) == list.end())
    list.push_back(traitName);
}

void Interpreter::ingestClasses(LoadedMod *m, std::vector<ClassDecl> &items, bool fromMod,
                   const std::string &modName, const std::string &atFile) {
  for (auto &c : items) {
    c.shape.name = c.name;
    c.shape.isPub = c.isPub;
    c.shape.line = c.line;
    c.shape.fields.clear();
    c.shape.fieldTypes.clear();
    for (const auto &f : c.fields) {
      c.shape.fields.push_back(f.name);
      c.shape.fieldTypes.push_back(f.type);
    }
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

void Interpreter::ingestImpls(std::vector<ImplDecl> &impls, const std::string &atFile) {
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

void Interpreter::flattenType(const std::string &name, std::vector<std::string> &stack,
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

void Interpreter::applyInheritance() {
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
Interpreter::lookupMethod(const std::string &typeName, const std::string &name) const {
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

void Interpreter::checkTraitImpls() {
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

void Interpreter::bindTraitSignals() {
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

void Interpreter::registerModShells(const ModDecl &block, const std::string &fullName) {
  if (!loaded.count(fullName))
    loaded[fullName] = LoadedMod{};
  for (const auto &child : block.mods)
    registerModShells(child, fullName + "." + child.name);
}

void Interpreter::loadImports(const std::vector<ImportDecl> &imports,
                 const std::string &fromFile, LoadedMod *owner) {
  for (const auto &im : imports)
    evalImport(im, fromFile, owner);
}

void Interpreter::ingestNestedMods(LoadedMod &parent, std::vector<ModDecl> &mods,
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

void Interpreter::ingestLoadedMod(ModDecl &block, const std::string &fullName,
                     const std::string &atFile) {
  LoadedMod dest;
  if (loaded.count(fullName))
    dest = std::move(loaded[fullName]);
  ingestFns(dest, block.fns, true, fullName, atFile);
  ingestStructs(dest, block.structs, true, fullName, atFile);
  ingestClasses(&dest, block.classes, true, fullName, atFile);
  ingestTraits(block.traits, atFile);
  ingestEnums(&dest, block.enums, true, fullName, atFile);
  ingestImpls(block.impls, atFile);
  ingestNestedMods(dest, block.mods, fullName, atFile);
  loadImports(block.imports, atFile, &dest);
  loaded[fullName] = std::move(dest);
}

void Interpreter::ingestModule(Program &p, const std::string &modName, LoadedMod &m,
                  const std::string &atFile) {
  ModDecl *block = pickMod(p, modName);
  const bool fromMod = block != nullptr;
  if (block) {
    ingestFns(m, block->fns, fromMod, modName, atFile);
    ingestStructs(m, block->structs, fromMod, modName, atFile);
    ingestClasses(&m, block->classes, fromMod, modName, atFile);
    ingestTraits(block->traits, atFile);
    ingestEnums(&m, block->enums, fromMod, modName, atFile);
    ingestImpls(block->impls, atFile);
    ingestNestedMods(m, block->mods, modName, atFile);
    loadImports(block->imports, atFile, &m);
  } else {
    ingestFns(m, p.fns, false, modName, atFile);
    ingestStructs(m, p.structs, false, modName, atFile);
    ingestClasses(&m, p.classes, false, modName, atFile);
    ingestTraits(p.traits, atFile);
    ingestEnums(&m, p.enums, false, modName, atFile);
    ingestImpls(p.impls, atFile);
    loadImports(p.imports, atFile, &m);
  }
}

void Interpreter::loadModule(const std::string &name, const std::string &fromFile, int line,
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

void Interpreter::bindFromImport(const ImportDecl &im, LoadedMod &mod, LoadedMod *owner,
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
  if (EnumDecl *en =
          mod.exportEnums.count(item) ? mod.exportEnums[item] : nullptr) {
    if (owner) {
      owner->enums[alias] = en;
      owner->exportEnums[alias] = en;
    } else {
      if (enums.count(alias) && enums[alias] != en)
        runtimeAt(fromFile, "duplicate enum '" + alias + "'", im.line,
                  im.col);
      enums[alias] = en;
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

void Interpreter::checkDottedExport(const ImportDecl &im, const std::string &fromFile) {
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

void Interpreter::evalImport(const ImportDecl &im, const std::string &fromFile,
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

void Interpreter::loadImports(Program &p, const std::string &fromFile, LoadedMod *owner) {
  loadImports(p.imports, fromFile, owner);
}
