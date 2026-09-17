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

Interpreter::Interpreter(Program p, std::string f, std::vector<std::string> a,
                         bool failFast)
    : program(std::move(p)), file(std::move(f)), argv(std::move(a)) {
  if (argv.empty() && !file.empty())
    argv.push_back(file);
  ingestEntry();
  loadImports(program, file, nullptr);
  applyInheritance();
  checkTraitImpls();
  checkAbstractFinal();
  bindTraitSignals();
  checkConstexprFns();
  typecheckAll();
  if (failFast && !diagnostics.empty())
    throw std::runtime_error(diagnosticError(diagnostics[0], file));
  env.emplace_back();
}

void Interpreter::ingestEntry() {
  const std::string crate = crateNameOfFile(file);
  if (!crate.empty()) {
    ingestCrateEntry(crate);
    return;
  }
  for (const auto &fn : program.fns)
    fns[fn.name] = &fn;
  for (auto &st : program.structs) {
    if (structs.count(st.name)) {
      loadFail(file, "duplicate struct '" + st.name + "'", st.line, 1);
      continue;
    }
    if (fns.count(st.name)) {
      loadFail(file, "struct '" + st.name + "' conflicts with a function",
               st.line, 1);
      continue;
    }
    structs[st.name] = &st;
    allTypes[st.name] = &st;
    recordFieldAccess(st.name, st.fields, st.fieldVis);
    for (const auto &m : st.methods)
      typeMethods[st.name][m.name] = &m;
    for (const auto &t : st.implTraits)
      recordTraitImpl(selfApplied(st.name, st.typeParams), t, st.typeParams,
                      st.line);
    registerTypeSignals(st.name, st.signals, file);
  }
  ingestClasses(nullptr, program.classes, false, "", file);
  ingestTraits(program.traits, file);
  ingestEnums(nullptr, program.enums, false, "", file);
  ingestImpls(program.impls, file);
  for (const auto &sig : program.signals) {
    if (signalArity.count(sig.name)) {
      loadFail(file, "duplicate signal '" + sig.name + "'", sig.line, 1);
      continue;
    }
    if (fns.count(sig.name)) {
      loadFail(file, "signal '" + sig.name + "' conflicts with a function",
               sig.line, 1);
      continue;
    }
    if (structs.count(sig.name) || allTypes.count(sig.name)) {
      loadFail(file, "signal '" + sig.name + "' conflicts with a struct",
               sig.line, 1);
      continue;
    }
    signalArity[sig.name] = sig.params.size();
    listeners[sig.name] = {};
  }
  for (auto &m : program.mods) {
    if (fns.count(m.name) || structs.count(m.name) ||
        allTypes.count(m.name) || enums.count(m.name) ||
        signalArity.count(m.name)) {
      loadFail(file, "module '" + m.name + "' conflicts with an existing name",
               m.line, 1);
      continue;
    }
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

std::filesystem::path findStdlibRoot(const std::string &fromFile) {
  namespace fs = std::filesystem;
  std::vector<fs::path> starts;
  if (!fromFile.empty()) {
    fs::path p = fs::path(fromFile).parent_path();
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

std::filesystem::path Interpreter::stdlibRoot() const {
  return findStdlibRoot(file);
}

static std::filesystem::path stdlibChildDir(const std::filesystem::path &root,
                                            const std::string &child) {
  namespace fs = std::filesystem;
  fs::path dir = root;
  std::string part;
  for (char c : child) {
    if (c == '.') {
      if (!part.empty()) {
        dir /= part;
        part.clear();
      }
    } else
      part.push_back(c);
  }
  if (!part.empty())
    dir /= part;
  return dir;
}

static bool sameRgFile(const std::string &a, const std::string &b) {
  namespace fs = std::filesystem;
  if (a.empty() || b.empty())
    return false;
  std::error_code ec;
  if (fs::equivalent(fs::path(a), fs::path(b), ec) && !ec)
    return true;
  auto norm = [](const std::string &p) {
    std::error_code nec;
    fs::path c = fs::weakly_canonical(fs::path(p), nec);
    if (nec)
      c = fs::path(p).lexically_normal();
    std::string s = c.generic_string();
    for (char &ch : s)
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
  };
  return norm(a) == norm(b);
}

std::string Interpreter::crateNameOfFile(const std::string &path) const {
  namespace fs = std::filesystem;
  if (path.empty())
    return "";
  const fs::path root = findStdlibRoot(path);
  if (root.empty())
    return "";
  std::error_code ec;
  fs::path filePath = fs::weakly_canonical(fs::path(path), ec);
  if (ec)
    filePath = fs::absolute(fs::path(path), ec);
  if (ec)
    filePath = fs::path(path);
  fs::path rootPath = fs::weakly_canonical(root, ec);
  if (ec)
    rootPath = root;
  const fs::path rel = fs::relative(filePath, rootPath, ec);
  if (ec)
    return "";
  const std::string s = rel.generic_string();
  if (s.empty() || s == ".")
    return "";
  if (s.size() >= 2 && s.compare(0, 2, "..") == 0)
    return "";
  const fs::path parent = rel.parent_path();
  if (parent.empty() || parent == ".")
    return "std";
  std::string crate = "std";
  for (const auto &part : parent) {
    crate += ".";
    crate += part.generic_string();
  }
  return crate;
}

std::filesystem::path Interpreter::crateDir(const std::string &name) const {
  const std::filesystem::path root = stdlibRoot();
  if (root.empty() || name == "std")
    return root;
  std::string child = name;
  if (child.size() > 4 && child.compare(0, 4, "std.") == 0)
    child = child.substr(4);
  else if (isStdlibChild(child)) {
    /* short crate name */
  } else
    return {};
  return stdlibChildDir(root, child);
}

static void indexStdlibSource(
    const std::string &source, const std::string &crate,
    std::map<std::string, std::vector<StdlibExport>> &out) {
  try {
    const auto tokens = tokenize(source, "");
    int depth = 0;
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
      const Tok k = tokens[i].kind;
      if (k == Tok::LBrace) {
        ++depth;
        continue;
      }
      if (k == Tok::RBrace) {
        if (depth > 0)
          --depth;
        continue;
      }
      if (depth != 0)
        continue;
      const char *kind = nullptr;
      switch (k) {
      case Tok::Function:
        kind = "fn";
        break;
      case Tok::Struct:
        kind = "struct";
        break;
      case Tok::Data:
        kind = "data";
        break;
      case Tok::Class:
        kind = "class";
        break;
      case Tok::Trait:
        kind = "trait";
        break;
      case Tok::Enum:
        kind = "enum";
        break;
      default:
        break;
      }
      if (!kind || tokens[i + 1].kind != Tok::Identifier)
        continue;
      const std::string &name = tokens[i + 1].text;
      auto &list = out[name];
      bool have = false;
      for (const auto &e : list) {
        if (e.crate == crate) {
          have = true;
          break;
        }
      }
      if (!have)
        list.push_back(StdlibExport{crate, kind});
    }
  } catch (...) {
  }
}

static void indexStdlibDir(
    const std::filesystem::path &dir, const std::string &crate,
    std::map<std::string, std::vector<StdlibExport>> &out) {
  namespace fs = std::filesystem;
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
  for (const auto &path : paths) {
    try {
      indexStdlibSource(readFile(path.generic_string()), crate, out);
    } catch (...) {
    }
  }
}

const std::map<std::string, std::vector<StdlibExport>> &
stdlibExportIndex(const std::string &fromFile) {
  static std::map<std::string, std::map<std::string, std::vector<StdlibExport>>>
      cache;
  const std::filesystem::path root = findStdlibRoot(fromFile);
  const std::string key = root.empty() ? std::string() : root.generic_string();
  auto it = cache.find(key);
  if (it != cache.end())
    return it->second;
  auto &idx = cache[key];
  if (root.empty())
    return idx;
  indexStdlibDir(root, "std", idx);
  const char *children[] = {"math", "str", "io",  "vec", "time",
                            "path", "json", "regex", "ui"};
  for (const char *child : children)
    indexStdlibDir(root / child, child, idx);
  return idx;
}

const StdlibExport *lookupStdlibExport(const std::string &name,
                                       const std::string &fromFile) {
  const auto &idx = stdlibExportIndex(fromFile);
  auto it = idx.find(name);
  if (it == idx.end() || it->second.empty())
    return nullptr;
  return &it->second[0];
}

std::string stdlibImportHint(const std::string &name,
                             const std::string &fromFile) {
  const auto &idx = stdlibExportIndex(fromFile);
  auto it = idx.find(name);
  if (it == idx.end() || it->second.empty())
    return "";
  const auto &hits = it->second;
  std::string crates;
  for (size_t i = 0; i < hits.size(); ++i) {
    if (i)
      crates += " or ";
    crates += hits[i].crate;
  }
  std::string tryMsg;
  if (hits.size() == 1) {
    const auto &h = hits[0];
    if (h.kind == "fn") {
      if (h.crate == "std")
        tryMsg = "; try 'import std' then 'std." + name + "'";
      else
        tryMsg = "; try 'import " + h.crate + "' then '" + h.crate + "." +
                 name + "'";
    } else if (h.crate == "std") {
      tryMsg = "; try 'import std'";
    } else {
      tryMsg = "; try 'from " + h.crate + " import " + name +
               "' or 'import " + h.crate + "'";
    }
  } else {
    tryMsg = "; try importing " + crates;
  }
  return " (in crate " + crates + tryMsg + ")";
}

std::vector<std::string> Interpreter::resolveModule(const std::string &name,
                                       const std::string &fromFile) {
  namespace fs = std::filesystem;
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
    if (root.empty())
      root = findStdlibRoot(fromFile);
    if (!root.empty()) {
      fs::path dir = root;
      if (stem != "std") {
        std::string child = stem;
        if (child.size() > 4 && child.compare(0, 4, "std.") == 0)
          child = child.substr(4);
        dir = stdlibChildDir(root, child);
      }
      addDir(dir);
      if (!out.empty())
        return out;
    }
  }

  std::string dotted = stem;
  for (char &c : dotted) {
    if (c == '.')
      c = static_cast<char>(fs::path::preferred_separator);
  }

  // Prefer the importing file's directory, then fall back to the entry
  // script directory so top-level packages still resolve from nested files.
  std::vector<fs::path> bases;
  auto pushBase = [&](fs::path base) {
    if (base.empty())
      base = ".";
    std::error_code ec;
    fs::path key = fs::weakly_canonical(base, ec);
    const std::string id =
        ec ? base.generic_string() : key.generic_string();
    for (const auto &existing : bases) {
      fs::path ek = fs::weakly_canonical(existing, ec);
      const std::string eid =
          ec ? existing.generic_string() : ek.generic_string();
      if (eid == id)
        return;
    }
    bases.push_back(std::move(base));
  };
  if (!fromFile.empty())
    pushBase(fs::path(fromFile).parent_path());
  pushBase(fs::path(file).parent_path());

  for (const auto &base : bases) {
    const size_t before = out.size();
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
    if (out.size() > before)
      return out;
  }
  return out;
}

std::string Interpreter::sandboxPath(const std::string &raw, int line,
                                     int col) const {
  namespace fs = std::filesystem;
  std::error_code ec;
  // Sandbox is the process working directory so relative paths keep their
  // existing meaning (scripts, assets, scratch files) while absolute paths
  // and .. escapes cannot leave the project tree.
  fs::path root = fs::current_path(ec);
  if (ec)
    runtime("cannot resolve sandbox root", line, col);
  fs::path rootCanon = fs::weakly_canonical(root, ec);
  if (ec)
    rootCanon = root;

  fs::path req(raw);
  if (req.empty())
    runtime("invalid path '" + raw + "'", line, col);
  if (req.is_relative())
    req = rootCanon / req;
  fs::path canon = fs::weakly_canonical(req, ec);
  if (ec) {
    fs::path parent = req.parent_path();
    if (parent.empty())
      parent = rootCanon;
    fs::path parentCanon = fs::weakly_canonical(parent, ec);
    if (ec)
      runtime("invalid path '" + raw + "'", line, col);
    canon = parentCanon / req.filename();
  }

  fs::path rel = fs::relative(canon, rootCanon, ec);
  if (ec || (!rel.empty() && *rel.begin() == ".."))
    runtime("path outside sandbox '" + raw + "'", line, col);
  return canon.generic_string();
}

namespace {

std::string absoluteSourcePath(const std::string &atFile) {
  if (atFile.empty())
    return atFile;
  std::error_code ec;
  auto abs = std::filesystem::absolute(atFile, ec);
  return ec ? atFile : abs.string();
}

void stampFnFile(FnDecl &fn, const std::string &atFile) {
  if (fn.file.empty() && !atFile.empty())
    fn.file = absoluteSourcePath(atFile);
}

} // namespace

void Interpreter::ingestFns(LoadedMod &m, std::vector<FnDecl> &fns, bool fromMod,
               const std::string &modName, const std::string &atFile) {
  for (auto &fn : fns) {
    fn.module = modName;
    stampFnFile(fn, atFile);
    if (m.fns.count(fn.name)) {
      FnDecl *prev = m.fns[fn.name];
      if (fn.isUfcs && prev && prev->isUfcs) {
        if (m.ufcsFns[fn.name].empty())
          m.ufcsFns[fn.name].push_back(prev);
        m.ufcsFns[fn.name].push_back(&fn);
        continue;
      }
      loadFail(atFile,
                "duplicate export '" + fn.name + "' in module '" +
                    modName + "'",
                fn.line, 1);
      continue;
    }
    m.fns[fn.name] = &fn;
    if (fn.isUfcs)
      m.ufcsFns[fn.name].push_back(&fn);
    if (!fromMod || fn.isPub)
      m.exports[fn.name] = &fn;
  }
}

void Interpreter::ingestStructs(LoadedMod &m, std::vector<structDecl> &items, bool fromMod,
                   const std::string &modName, const std::string &atFile) {
  for (auto &st : items) {
    if (m.structs.count(st.name)) {
      loadFail(atFile,
                "duplicate export '" + st.name + "' in module '" +
                    modName + "'",
                st.line, 1);
      continue;
    }
    m.structs[st.name] = &st;
    allTypes[st.name] = &st;
    recordFieldAccess(st.name, st.fields, st.fieldVis);
    for (auto &method : st.methods) {
      method.module = modName;
      stampFnFile(method, atFile);
      typeMethods[st.name][method.name] = &method;
    }
    for (const auto &t : st.implTraits)
      recordTraitImpl(selfApplied(st.name, st.typeParams), t, st.typeParams,
                      st.line);
    registerTypeSignals(st.name, st.signals, atFile);
    if (!fromMod || st.isPub) {
      m.exportStructs[st.name] = &st;
      if (structs.count(st.name))
        loadFail(atFile, "duplicate struct '" + st.name + "'", st.line, 1);
      if (fns.count(st.name))
        loadFail(atFile,
                  "struct '" + st.name + "' conflicts with a function",
                  st.line, 1);
      structs[st.name] = &st;
    }
  }
}

void Interpreter::ingestTraits(std::vector<TraitDecl> &items, const std::string &atFile) {
  for (auto &t : items) {
    if (traits.count(t.name)) {
      loadFail(atFile, "duplicate trait '" + t.name + "'", t.line, 1);
      continue;
    }
    traits[t.name] = &t;
    registerTypeSignals(t.name, t.signals, atFile);
  }
}

void Interpreter::ingestEnums(LoadedMod *m, std::vector<EnumDecl> &items, bool fromMod,
                 const std::string &modName, const std::string &atFile) {
  for (auto &e : items) {
    if (m) {
      if (m->enums.count(e.name))
        loadFail(atFile,
                  "duplicate export '" + e.name + "' in module '" + modName +
                      "'",
                  e.line, 1);
      m->enums[e.name] = &e;
    }
    if (fns.count(e.name))
      loadFail(atFile, "enum '" + e.name + "' conflicts with a function",
                e.line, 1);
    if (structs.count(e.name) || allTypes.count(e.name))
      loadFail(atFile, "enum '" + e.name + "' conflicts with a type", e.line,
                1);
    if (!m || !fromMod || e.isPub) {
      if (m)
        m->exportEnums[e.name] = &e;
      if (enums.count(e.name))
        loadFail(atFile, "duplicate enum '" + e.name + "'", e.line, 1);
      enums[e.name] = &e;
    }
  }
}

void Interpreter::recordTypeTrait(const std::string &typeName,
                     const std::string &traitName) {
  auto &list = typeTraits[typeHead(typeName)];
  if (std::find(list.begin(), list.end(), traitName) == list.end())
    list.push_back(traitName);
}

void Interpreter::recordTraitImpl(const std::string &typeName,
                                  const std::string &traitName,
                                  const std::vector<TypeParam> &params,
                                  int line) {
  recordTypeTrait(typeName, traitName);
  TraitImplInfo info;
  info.typeParams = params;
  info.typeName = typeName;
  info.traitName = traitName;
  info.line = line;
  traitImpls.push_back(std::move(info));
}

void Interpreter::registerTypeSignals(const std::string &typeName,
                                      const std::vector<SignalDecl> &sigs,
                                      const std::string &atFile) {
  const structDecl *st = nullptr;
  auto tit = allTypes.find(typeName);
  if (tit != allTypes.end())
    st = tit->second;
  auto mit = typeMethods.find(typeName);
  for (const auto &sig : sigs) {
    if (st) {
      for (const auto &f : st->fields) {
        if (f == sig.name)
          loadFail(atFile,
                   "signal '" + sig.name + "' conflicts with field '" +
                       sig.name + "' on " + typeName,
                   sig.line, 1);
      }
    }
    if (mit != typeMethods.end() && mit->second.count(sig.name))
      loadFail(atFile,
               "signal '" + sig.name + "' conflicts with method '" +
                   sig.name + "' on " + typeName,
               sig.line, 1);
    auto &slot = typeSignals[typeName];
    if (slot.count(sig.name)) {
      loadFail(atFile, "duplicate signal '" + sig.name + "' on " + typeName,
               sig.line, 1);
      continue;
    }
    slot[sig.name] = sig.params.size();
  }
}

std::optional<std::size_t>
Interpreter::lookupTypeSignal(const std::string &typeName,
                              const std::string &name) const {
  std::string current = typeHead(typeName);
  std::set<std::string> seen;
  while (!current.empty()) {
    if (!seen.insert(current).second)
      break;
    auto it = typeSignals.find(current);
    if (it != typeSignals.end()) {
      auto sit = it->second.find(name);
      if (sit != it->second.end())
        return sit->second;
    }
    auto pit = classParents.find(current);
    if (pit == classParents.end())
      break;
    current = typeHead(pit->second);
  }
  return std::nullopt;
}

void Interpreter::initInstanceSignals(StructData &data) const {
  std::string current = data.name;
  std::set<std::string> seen;
  while (!current.empty()) {
    if (!seen.insert(current).second)
      break;
    auto it = typeSignals.find(current);
    if (it != typeSignals.end()) {
      for (const auto &kv : it->second) {
        if (!data.listeners.count(kv.first))
          data.listeners[kv.first] = {};
      }
    }
    auto pit = classParents.find(current);
    if (pit == classParents.end())
      break;
    current = typeHead(pit->second);
  }
}

void Interpreter::ingestClasses(LoadedMod *m, std::vector<ClassDecl> &items, bool fromMod,
                   const std::string &modName, const std::string &atFile) {
  for (auto &c : items) {
    c.shape.name = c.name;
    c.shape.isPub = c.isPub;
    c.shape.line = c.line;
    c.shape.typeParams = c.typeParams;
    c.shape.fields.clear();
    c.shape.fieldTypes.clear();
    c.shape.fieldOptional.clear();
    for (const auto &f : c.fields) {
      c.shape.fields.push_back(f.name);
      c.shape.fieldTypes.push_back(f.type);
      c.shape.fieldOptional.push_back(f.optional ? 1 : 0);
    }
    if (m) {
      if (m->structs.count(c.name))
        loadFail(atFile,
                  "duplicate export '" + c.name + "' in module '" + modName +
                      "'",
                  c.line, 1);
      m->structs[c.name] = &c.shape;
    }
    if (allTypes.count(c.name)) {
      loadFail(atFile, "duplicate class '" + c.name + "'", c.line, 1);
      continue;
    }
    if (fns.count(c.name)) {
      loadFail(atFile, "class '" + c.name + "' conflicts with a function",
                c.line, 1);
      continue;
    }
    allTypes[c.name] = &c.shape;
    for (const auto &f : c.fields) {
      if (f.hasDefault)
        fieldDefaults[c.name][f.name] = &f.defaultValue;
      fieldAccess[c.name][f.name] = f.vis;
    }
    if (!c.parent.empty())
      classParents[c.name] = c.parent;
    classAbstract[c.name] = c.isAbstract;
    classFinal[c.name] = c.isFinal;
    auto &slot = typeMethods[c.name];
    for (auto &method : c.methods) {
      method.module = modName;
      stampFnFile(method, atFile);
      if (slot.count(method.name))
        loadFail(atFile,
                  "duplicate method '" + method.name + "' on " + c.name,
                  method.line, 1);
      slot[method.name] = &method;
    }
    for (auto &block : c.traitImpls) {
      std::vector<TypeParam> params = c.typeParams;
      params.insert(params.end(), block.typeParams.begin(),
                    block.typeParams.end());
      recordTraitImpl(selfApplied(c.name, c.typeParams), block.traitName,
                      params, c.line);
      for (auto &method : block.methods) {
        method.module = modName;
        stampFnFile(method, atFile);
        if (slot.count(method.name))
          loadFail(atFile,
                    "duplicate method '" + method.name + "' on " + c.name,
                    method.line, 1);
        slot[method.name] = &method;
      }
    }
    for (const auto &t : c.implTraits)
      recordTraitImpl(selfApplied(c.name, c.typeParams), t, c.typeParams,
                      c.line);
    c.shape.signals = c.signals;
    registerTypeSignals(c.name, c.signals, atFile);
    if (!m || !fromMod || c.isPub) {
      if (m)
        m->exportStructs[c.name] = &c.shape;
      if (structs.count(c.name))
        loadFail(atFile, "duplicate class '" + c.name + "'", c.line, 1);
      structs[c.name] = &c.shape;
    }
  }
}

void Interpreter::ingestImpls(std::vector<ImplDecl> &impls, const std::string &atFile) {
  for (auto &im : impls) {
    const std::string head = typeHead(im.typeName);
    auto &slot = typeMethods[head];
    const structDecl *st = nullptr;
    auto git = allTypes.find(head);
    if (git != allTypes.end())
      st = git->second;
    else {
      auto sit = structs.find(head);
      if (sit != structs.end())
        st = sit->second;
    }
    if (!st) {
      loadFail(atFile,
               "undefined struct '" + head + "'" +
                   stdlibImportHint(head, atFile),
               im.line, 1);
      continue;
    }
    if (!im.traitName.empty()) {
      if (!traits.count(typeHead(im.traitName)))
        loadFail(atFile,
                 "undefined trait '" + typeHead(im.traitName) + "'" +
                     stdlibImportHint(typeHead(im.traitName), atFile),
                 im.line, 1);
      else {
        std::vector<TypeParam> params = im.typeParams;
        if (params.empty())
          params = st->typeParams;
        recordTraitImpl(im.typeName, im.traitName, params, im.line);
      }
    }
    for (auto &method : im.methods) {
      stampFnFile(method, atFile);
      if (st) {
        for (const auto &fld : st->fields) {
          if (fld == method.name)
            loadFail(atFile,
                      "method '" + method.name + "' conflicts with field '" +
                          method.name + "' on " + head,
                      method.line, 1);
        }
      }
      if (slot.count(method.name))
        loadFail(atFile,
                  "duplicate method '" + method.name + "' on " + head,
                  method.line, 1);
      if (method.vis == Vis::Protected && !classAbstract.count(head))
        loadFail(atFile,
                 std::string("protected cannot apply to ") +
                     (st && st->isData ? "data" : "struct") + " method",
                 method.line, 1);
      slot[method.name] = &method;
    }
  }
}

void Interpreter::flattenType(const std::string &name, std::vector<std::string> &stack,
                 std::set<std::string> &done) {
  if (done.count(name))
    return;
  auto typeLine = [&]() {
    auto it = allTypes.find(name);
    if (it != allTypes.end() && it->second)
      return it->second->line;
    return 1;
  };
  if (std::find(stack.begin(), stack.end(), name) != stack.end()) {
    loadFail(file, "cycle in class inheritance at '" + name + "'", typeLine(),
             1);
    return;
  }
  auto pit = classParents.find(name);
  if (pit == classParents.end()) {
    done.insert(name);
    return;
  }
  const std::string parentApplied = pit->second;
  const std::string parent = typeHead(parentApplied);
  if (!allTypes.count(parent)) {
    if (typeMethods.count(parent)) {
      done.insert(name);
      return;
    }
    loadFail(file,
             "class '" + name + "' extends unknown type '" + parentApplied +
                 "'" + stdlibImportHint(parent, file),
             typeLine(), 1);
    return;
  }
  if (allTypes[parent]->isData) {
    loadFail(file,
             "class '" + name + "' cannot extend data type '" + parent + "'",
             typeLine(), 1);
    return;
  }
  stack.push_back(name);
  flattenType(parent, stack, done);
  stack.pop_back();
  structDecl *child = const_cast<structDecl *>(allTypes[name]);
  const structDecl *parentDef = allTypes[parent];
  const auto env =
      typeEnvFrom(parentDef->typeParams, typeArgList(parentApplied));
  std::vector<std::string> fields = parentDef->fields;
  std::vector<std::string> types = parentDef->fieldTypes;
  std::vector<char> opts = parentDef->fieldOptional;
  types.resize(fields.size());
  opts.resize(fields.size());
  for (auto &ty : types)
    ty = substType(ty, env);
  types.resize(fields.size());
  opts.resize(fields.size());
  for (size_t i = 0; i < child->fields.size(); ++i) {
    const std::string &f = child->fields[i];
    if (std::find(fields.begin(), fields.end(), f) != fields.end())
      continue;
    fields.push_back(f);
    types.push_back(i < child->fieldTypes.size() ? child->fieldTypes[i] : "");
    opts.push_back(i < child->fieldOptional.size() ? child->fieldOptional[i]
                                                   : 0);
  }
  child->fields = std::move(fields);
  child->fieldTypes = std::move(types);
  child->fieldOptional = std::move(opts);
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
  std::string current = typeHead(typeName);
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
    current = typeHead(pit->second);
  }
  return {"", nullptr};
}

void Interpreter::recordFieldAccess(const std::string &typeName,
                                    const std::vector<std::string> &fields,
                                    const std::vector<char> &vis) {
  auto &slot = fieldAccess[typeName];
  for (size_t i = 0; i < fields.size(); ++i) {
    Vis v = Vis::Pub;
    if (i < vis.size())
      v = static_cast<Vis>(vis[i]);
    slot[fields[i]] = v;
  }
}

std::pair<std::string, Vis>
Interpreter::lookupField(const std::string &typeName,
                         const std::string &name) const {
  std::string current = typeHead(typeName);
  std::set<std::string> seen;
  while (!current.empty()) {
    if (!seen.insert(current).second)
      break;
    auto typeIt = fieldAccess.find(current);
    if (typeIt != fieldAccess.end()) {
      auto fit = typeIt->second.find(name);
      if (fit != typeIt->second.end())
        return {current, fit->second};
    }
    auto pit = classParents.find(current);
    if (pit == classParents.end())
      break;
    current = typeHead(pit->second);
  }
  return {"", Vis::Pub};
}

void Interpreter::checkTraitImpls() {
  for (const auto &kv : typeTraits) {
    int typeLine = 1;
    auto titType = allTypes.find(kv.first);
    if (titType != allTypes.end() && titType->second)
      typeLine = titType->second->line;
    for (const auto &traitName : kv.second) {
      auto tit = traits.find(typeHead(traitName));
      if (tit == traits.end()) {
        loadFail(file,
                 "undefined trait '" + typeHead(traitName) + "'" +
                     stdlibImportHint(typeHead(traitName), file),
                 typeLine, 1);
        continue;
      }
      const auto targs = typeArgList(traitName);
      if (!targs.empty()) {
        if (tit->second->typeParams.empty())
          loadFail(file,
                   "'" + typeHead(traitName) + "' does not take type arguments",
                   typeLine, 1);
        else if (targs.size() != tit->second->typeParams.size())
          loadFail(file,
                   "'" + typeHead(traitName) + "' expected " +
                       std::to_string(tit->second->typeParams.size()) +
                       " type argument(s), got " +
                       std::to_string(targs.size()),
                   typeLine, 1);
      }
      for (const auto &m : tit->second->methods) {
        if (!lookupMethod(kv.first, m.name).second)
          loadFail(file,
                   "type '" + kv.first + "' is missing '" + m.name +
                       "' for trait '" + traitName + "'",
                   m.line > 0 ? m.line : typeLine, 1);
      }
    }
  }
}

void Interpreter::checkAbstractFinal() {
  auto typeLine = [&](const std::string &name) {
    auto it = allTypes.find(name);
    if (it != allTypes.end() && it->second)
      return it->second->line;
    return 1;
  };

  for (const auto &kv : classParents) {
    auto fit = classFinal.find(typeHead(kv.second));
    if (fit != classFinal.end() && fit->second)
      loadFail(file,
               "class '" + kv.first + "' extends final class '" +
                   typeHead(kv.second) + "'",
               typeLine(kv.first), 1);
  }

  for (const auto &kv : classAbstract) {
    const std::string &name = kv.first;
    const bool absClass = kv.second;
    const bool finClass = classFinal.count(name) && classFinal[name];
    const int line = typeLine(name);
    if (absClass && finClass)
      loadFail(file,
               "class '" + name + "' cannot be both abstract and final", line,
               1);

    auto mit = typeMethods.find(name);
    if (mit != typeMethods.end()) {
      for (const auto &m : mit->second) {
        if (m.second->isAbstract && m.second->isFinal)
          loadFail(file,
                   "method '" + m.first +
                       "' cannot be both abstract and final",
                   m.second->line, 1);
        if (m.second->isAbstract && !absClass)
          loadFail(file,
                   "class '" + name + "' has abstract method '" + m.first +
                       "' but is not abstract",
                   m.second->line, 1);

        std::string current =
            classParents.count(name) ? typeHead(classParents[name]) : "";
        std::set<std::string> walked;
        while (!current.empty() && walked.insert(current).second) {
          auto tmit = typeMethods.find(current);
          if (tmit != typeMethods.end()) {
            auto parentM = tmit->second.find(m.first);
            if (parentM != tmit->second.end()) {
              if (parentM->second->isFinal)
                loadFail(file,
                         "cannot override final method '" + m.first + "'",
                         m.second->line, 1);
              break;
            }
          }
          auto pit = classParents.find(current);
          if (pit == classParents.end())
            break;
          current = typeHead(pit->second);
        }
      }
    }

    if (absClass)
      continue;

    std::set<std::string> seen;
    std::string current = name;
    std::set<std::string> walked;
    while (!current.empty() && walked.insert(current).second) {
      auto tmit = typeMethods.find(current);
      if (tmit != typeMethods.end()) {
        for (const auto &m : tmit->second) {
          if (!seen.insert(m.first).second)
            continue;
          if (m.second->isAbstract)
            loadFail(file,
                     "type '" + name + "' is missing abstract method '" +
                         m.first + "'",
                     line, 1);
        }
      }
      auto pit = classParents.find(current);
      if (pit == classParents.end())
        break;
      current = typeHead(pit->second);
    }
  }
}

void Interpreter::bindTraitSignals() {
  for (const auto &kv : typeTraits) {
    int typeLine = 1;
    auto titType = allTypes.find(kv.first);
    if (titType != allTypes.end() && titType->second)
      typeLine = titType->second->line;
    for (const auto &traitName : kv.second) {
      auto tit = traits.find(typeHead(traitName));
      if (tit == traits.end())
        continue;
      for (const auto &sig : tit->second->signals) {
        auto &slot = typeSignals[kv.first];
        if (slot.count(sig.name)) {
          if (slot[sig.name] != sig.params.size())
            loadFail(file,
                     "signal '" + sig.name + "' on " + kv.first +
                         " conflicts with trait '" + traitName + "'",
                     sig.line, 1);
          continue;
        }
        auto mit = typeMethods.find(kv.first);
        if (mit != typeMethods.end() && mit->second.count(sig.name))
          loadFail(file,
                   "signal '" + sig.name + "' conflicts with method '" +
                       sig.name + "' on " + kv.first,
                   typeLine, 1);
        const structDecl *st =
            titType != allTypes.end() ? titType->second : nullptr;
        if (st) {
          for (const auto &f : st->fields) {
            if (f == sig.name)
              loadFail(file,
                       "signal '" + sig.name + "' conflicts with field '" +
                           sig.name + "' on " + kv.first,
                       typeLine, 1);
          }
        }
        slot[sig.name] = sig.params.size();
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

void Interpreter::ingestCrateEntry(const std::string &crate) {
  loading.push_back(crate);
  LoadedMod m;
  try {
    for (const auto &path : resolveModule(crate, file)) {
      if (sameRgFile(path, file))
        continue;
      std::vector<Diagnostic> parseErrs;
      Program *kept = keep(parseSource(readFile(path), path, &parseErrs));
      for (const auto &d : parseErrs)
        recordDiag(d.kind, d.file, d.line, d.col, d.message);
      ingestModule(*kept, crate, m, path);
    }
    ingestModule(program, crate, m, file);
  } catch (...) {
    loading.pop_back();
    throw;
  }
  loading.pop_back();
  loaded[crate] = std::move(m);
  attachCrateChildren(crate);
  const auto dot = crate.rfind('.');
  const std::string bind = dot == std::string::npos ? crate : crate.substr(dot + 1);
  moduleBinds[bind] = crate;
}

void Interpreter::attachCrateChildren(const std::string &parent) {
  namespace fs = std::filesystem;
  auto it = loaded.find(parent);
  if (it == loaded.end())
    return;
  const fs::path dir = crateDir(parent);
  std::error_code ec;
  if (dir.empty() || !fs::is_directory(dir, ec) || ec)
    return;
  std::vector<std::string> kids;
  for (const auto &entry : fs::directory_iterator(dir, ec)) {
    if (ec)
      break;
    std::error_code dec;
    if (!entry.is_directory(dec) || dec)
      continue;
    const std::string kid = entry.path().filename().generic_string();
    if (kid.empty() || kid[0] == '.')
      continue;
    kids.push_back(kid);
  }
  std::sort(kids.begin(), kids.end());
  for (const std::string &kid : kids) {
    const std::string full = parent + "." + kid;
    if (resolveModule(full, file).empty())
      continue;
    loadModule(full, file, 1, 1);
    it = loaded.find(parent);
    if (it == loaded.end())
      return;
    if (!loaded.count(full))
      continue;
    it->second.modules[kid] = full;
    it->second.exportMods[kid] = full;
  }
}

void Interpreter::loadModule(const std::string &raw, const std::string &fromFile, int line,
                int col) {
  const std::string name = canonicalStdlibName(raw);
  if (loaded.count(name) || isHostModule(name))
    return;
  for (const auto &cur : loading) {
    if (cur == name) {
      loadFail(fromFile, "cyclic import of module '" + name + "'", line,
                col);
      return;
    }
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
  if (files.empty()) {
    loadFail(fromFile,
              "module '" + name + "' not found (tried " + name +
                  "/ or mod " + name + ")",
              line, col);
    return;
  }

  loading.push_back(name);
  LoadedMod m;
  try {
    for (const auto &path : files) {
      std::vector<Diagnostic> parseErrs;
      Program *kept = keep(parseSource(readFile(path), path, &parseErrs));
      for (const auto &d : parseErrs)
        recordDiag(d.kind, d.file, d.line, d.col, d.message);
      ingestModule(*kept, name, m, path);
    }
  } catch (...) {
    loading.pop_back();
    throw;
  }
  loading.pop_back();
  loaded[name] = std::move(m);
  attachCrateChildren(name);
}

void Interpreter::bindFromImport(const ImportDecl &im, LoadedMod &mod, LoadedMod *owner,
                    const std::string &fromFile) {
  if (im.path.size() != 2) {
    loadFail(fromFile,
              "nested from-imports longer than 2 segments are not supported",
              im.line, im.col);
    return;
  }
  const std::string &item = im.path[1];
  const std::string alias = im.alias.empty() ? item : im.alias;
  if (FnDecl *fn = mod.exports.count(item) ? mod.exports[item] : nullptr) {
    if (owner) {
      if (owner->fromFns.count(alias) || owner->fns.count(alias)) {
        loadFail(fromFile, "duplicate import '" + alias + "'", im.line,
                  im.col);
        return;
      }
      owner->fromFns[alias] = fn;
    } else {
      if (fns.count(alias)) {
        loadFail(fromFile, "duplicate import '" + alias + "'", im.line,
                  im.col);
        return;
      }
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
      if (structs.count(alias) && structs[alias] != st) {
        loadFail(fromFile, "duplicate struct '" + alias + "'", im.line,
                  im.col);
        return;
      }
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
      if (enums.count(alias) && enums[alias] != en) {
        loadFail(fromFile, "duplicate enum '" + alias + "'", im.line,
                  im.col);
        return;
      }
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
  loadFail(fromFile,
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
      if (!pit->second.exportMods.count(seg)) {
        loadFail(fromFile,
                  "module '" + acc + "' has no export '" + seg + "'",
                  im.line, im.col);
        return;
      }
    } else {
      return;
    }
    acc += ".";
    acc += seg;
  }
}

void Interpreter::evalImport(const ImportDecl &im, const std::string &fromFile,
                LoadedMod *owner) {
  if (im.path.empty()) {
    loadFail(fromFile, "empty import", im.line, im.col);
    return;
  }
  if (isHostModule(im.path[0]))
    return;
  if (im.isFrom) {
    const std::string key = canonicalStdlibName(im.path[0]);
    loadModule(key, fromFile, im.line, im.col);
    auto it = loaded.find(key);
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
  const std::string key = canonicalStdlibName(full);
  loadModule(key, fromFile, im.line, im.col);
  checkDottedExport(im, fromFile);
  const std::string bind =
      im.alias.empty() ? im.path.back() : im.alias;
  auto setBind = [&](const std::string &name, const std::string &target) {
    if (owner)
      owner->modules[name] = target;
    else
      moduleBinds[name] = target;
  };
  setBind(bind, key);
  // Short crate imports (`import regex`) rewrite to `std.regex` and should
  // also bind `std`, matching `import std.regex` / documented `import math`.
  if (im.path[0] == "std" || key == "std" ||
      (key.size() > 4 && key.compare(0, 4, "std.") == 0))
    setBind("std", "std");
}

void Interpreter::loadImports(Program &p, const std::string &fromFile, LoadedMod *owner) {
  loadImports(p.imports, fromFile, owner);
}
