#include "interp.h"
#include "parser.h"
#include "format.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

RunResult runFile(const std::string &path, const std::vector<std::string> &argv) {
  try {
    std::vector<Diagnostic> parseErrs;
    Program program = parseSource(readFile(path), path, &parseErrs);
    if (!parseErrs.empty())
      throw std::runtime_error(diagnosticError(parseErrs[0], path));
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
    std::vector<Diagnostic> parseErrs;
    Program program = parseSource(readFile(path), path, &parseErrs);
    if (!parseErrs.empty())
      throw std::runtime_error(diagnosticError(parseErrs[0], path));
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
      std::vector<Diagnostic> parseErrs;
      Program program = parseSource(readFile(name), name, &parseErrs);
      if (!parseErrs.empty())
        throw std::runtime_error(diagnosticError(parseErrs[0], name));
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

static Diagnostic diagnosticFromMessage(const std::string &file,
                                        const std::string &err) {
  Diagnostic d;
  d.file = file;
  d.line = 1;
  d.col = 1;
  d.severity = "error";
  d.message = err;

  const auto at = err.rfind(" at ");
  if (at == std::string::npos)
    return d;

  const std::string after = err.substr(at + 4);
  size_t i = 0;
  while (i < after.size() &&
         std::isdigit(static_cast<unsigned char>(after[i])))
    ++i;
  if (i == 0 || i >= after.size() || after[i] != ':')
    return d;
  size_t j = i + 1;
  while (j < after.size() &&
         std::isdigit(static_cast<unsigned char>(after[j])))
    ++j;
  if (j == i + 1)
    return d;

  d.line = std::stoi(after.substr(0, i));
  d.col = std::stoi(after.substr(i + 1, j - i - 1));
  if (d.line < 1)
    d.line = 1;
  if (d.col < 1)
    d.col = 1;

  std::string rest = after.substr(j);
  if (!rest.empty() && rest[0] == ':') {
    size_t k = 1;
    while (k < rest.size() && rest[k] == ' ')
      ++k;
    d.message = rest.substr(k);
  }

  const auto in = err.rfind(" in ", at);
  if (in != std::string::npos && in < at)
    d.file = err.substr(in + 4, at - (in + 4));
  if (d.message.empty())
    d.message = err;
  return d;
}

static std::string jsonEscape(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 8);
  const char *hex = "0123456789abcdef";
  for (unsigned char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\b':
      out += "\\b";
      break;
    case '\f':
      out += "\\f";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (c < 0x20) {
        out += "\\u00";
        out.push_back(hex[c >> 4]);
        out.push_back(hex[c & 15]);
      } else {
        out.push_back(static_cast<char>(c));
      }
    }
  }
  return out;
}

std::string diagnosticToHuman(const Diagnostic &d) {
  std::ostringstream ss;
  if (!d.file.empty())
    ss << d.file << ":";
  ss << d.line << ":" << d.col << ": " << d.severity << ": " << d.message;
  return ss.str();
}

std::string diagnosticsToJson(const std::vector<Diagnostic> &diags) {
  if (diags.empty())
    return "[]\n";
  std::ostringstream ss;
  ss << "[\n";
  for (size_t i = 0; i < diags.size(); ++i) {
    const Diagnostic &d = diags[i];
    if (i)
      ss << ",\n";
    ss << "  {\n";
    ss << "    \"file\": \"" << jsonEscape(d.file) << "\",\n";
    ss << "    \"line\": " << d.line << ",\n";
    ss << "    \"col\": " << d.col << ",\n";
    ss << "    \"severity\": \"" << jsonEscape(d.severity) << "\",\n";
    ss << "    \"message\": \"" << jsonEscape(d.message) << "\"\n";
    ss << "  }";
  }
  ss << "\n]\n";
  return ss.str();
}

std::vector<Diagnostic> checkSource(const std::string &source,
                                    const std::string &path) {
  try {
    std::vector<Diagnostic> diags;
    Program program = parseSource(source, path, &diags);
    Interpreter interp(std::move(program), path, {path}, false);
    diags.insert(diags.end(), interp.diagnostics.begin(),
                 interp.diagnostics.end());
    return diags;
  } catch (const std::exception &ex) {
    return {diagnosticFromMessage(path, ex.what())};
  }
}

std::vector<Diagnostic> checkFile(const std::string &path) {
  try {
    return checkSource(readFile(path), path);
  } catch (const std::exception &ex) {
    return {diagnosticFromMessage(path, ex.what())};
  }
}

static void appendCheckSelfTest(RunResult &result) {
  auto require = [&](bool ok, const std::string &name,
                     const std::string &detail) {
    if (ok) {
      result.out += "ok   check " + name + "\n";
      return;
    }
    result.ok = false;
    result.out += "FAIL check " + name + ": " + detail + "\n";
  };

  std::error_code ec;
  if (fs::is_regular_file("examples/hello.rg", ec)) {
    auto d = checkFile("examples/hello.rg");
    require(d.empty(), "clean",
            d.empty() ? "" : diagnosticToHuman(d[0]));
  }
  if (fs::is_regular_file("tests/fail/type_add.rg", ec)) {
    auto d = checkFile("tests/fail/type_add.rg");
    const bool hit =
        !d.empty() && d[0].message.find("cannot add") != std::string::npos;
    require(hit, "type_add",
            d.empty() ? "no diagnostic" : d[0].message);
  }
  if (fs::is_regular_file("tests/fail/type_multi.rg", ec)) {
    auto d = checkFile("tests/fail/type_multi.rg");
    bool add = false;
    bool cmp = false;
    for (const auto &x : d) {
      if (x.message.find("cannot add") != std::string::npos)
        add = true;
      if (x.message.find("cannot compare") != std::string::npos)
        cmp = true;
    }
    require(d.size() >= 2 && add && cmp, "type_multi",
            d.empty() ? "no diagnostic"
                      : std::to_string(d.size()) + " diags: " + d[0].message);
  }
  if (fs::is_regular_file("tests/fail/parse_expr.rg", ec)) {
    auto d = checkFile("tests/fail/parse_expr.rg");
    const bool hit = !d.empty() &&
                     d[0].message.find("expected expression") !=
                         std::string::npos;
    require(hit, "parse", d.empty() ? "no diagnostic" : d[0].message);
  }
  if (fs::is_regular_file("tests/fail/parse_multi.rg", ec)) {
    auto d = checkFile("tests/fail/parse_multi.rg");
    int n = 0;
    for (const auto &x : d) {
      if (x.message.find("expected expression") != std::string::npos)
        ++n;
    }
    require(n >= 2, "parse_multi",
            d.empty() ? "no diagnostic"
                      : std::to_string(d.size()) + " diags: " + d[0].message);
  }
  if (fs::is_regular_file("tests/fail/constexpr_multi.rg", ec)) {
    auto d = checkFile("tests/fail/constexpr_multi.rg");
    bool var = false;
    bool print = false;
    for (const auto &x : d) {
      if (x.message.find("var") != std::string::npos)
        var = true;
      if (x.message.find("print") != std::string::npos)
        print = true;
    }
    require(d.size() >= 2 && var && print, "constexpr_multi",
            d.empty() ? "no diagnostic"
                      : std::to_string(d.size()) + " diags: " + d[0].message);
  }
  if (fs::is_regular_file("tests/fail/load_multi.rg", ec)) {
    auto d = checkFile("tests/fail/load_multi.rg");
    int n = 0;
    for (const auto &x : d) {
      if (x.message.find("duplicate struct") != std::string::npos)
        ++n;
    }
    require(n >= 2, "load_multi",
            d.empty() ? "no diagnostic"
                      : std::to_string(d.size()) + " diags: " + d[0].message);
  }
  if (fs::is_regular_file("tests/fail/match_multi.rg", ec)) {
    auto d = checkFile("tests/fail/match_multi.rg");
    int n = 0;
    for (const auto &x : d) {
      if (x.message.find("missing variant") != std::string::npos)
        ++n;
    }
    require(n >= 2, "match_multi",
            d.empty() ? "no diagnostic"
                      : std::to_string(d.size()) + " diags: " + d[0].message);
  }
  if (fs::is_regular_file("tests/fail/crate_std.rg", ec)) {
    auto d = checkFile("tests/fail/crate_std.rg");
    bool trait = false;
    for (const auto &x : d) {
      if (x.message.find("Identifiable") != std::string::npos &&
          x.message.find("in crate std") != std::string::npos)
        trait = true;
    }
    require(trait, "crate_std",
            d.empty() ? "no diagnostic" : d[0].message);
  }
  if (fs::is_regular_file("tests/fail/crate_uuid.rg", ec)) {
    auto d = checkFile("tests/fail/crate_uuid.rg");
    bool uuid = false;
    for (const auto &x : d) {
      if (x.message.find("UUID") != std::string::npos &&
          x.message.find("in crate std") != std::string::npos)
        uuid = true;
    }
    require(uuid, "crate_uuid",
            d.empty() ? "no diagnostic" : d[0].message);
  }
  if (fs::is_regular_file("builtin/std/ui/color.rg", ec)) {
    auto d = checkFile("builtin/std/ui/color.rg");
    require(d.empty(), "ui_color_crate",
            d.empty() ? "" : diagnosticToHuman(d[0]));
  }
  if (fs::is_regular_file("builtin/std/ui/lib.rg", ec)) {
    auto d = checkFile("builtin/std/ui/lib.rg");
    require(d.empty(), "ui_lib_crate",
            d.empty() ? "" : diagnosticToHuman(d[0]));
  }
  if (fs::is_regular_file("builtin/std/ui/widgets.rg", ec)) {
    auto d = checkFile("builtin/std/ui/widgets.rg");
    require(d.empty(), "ui_widgets_crate",
            d.empty() ? "" : diagnosticToHuman(d[0]));
  }
  {
    const std::string src = "fn main():Int{\nreturn 0;\n}\n";
    FormatResult f = formatSource(src, "fmt_check.rg");
    require(f.ok, "fmt_parse", f.message);
    require(f.out.find("fn main(): Int") != std::string::npos, "fmt_space",
            f.out);
    FormatResult again = formatSource(f.out, "fmt_check.rg");
    require(again.ok && again.out == f.out, "fmt_idempotent",
            again.ok ? "output changed on second format" : again.message);
  }
  require(jsonRpcSelfTest(), "jsonrpc", "parser/encode failed");
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
  appendCheckSelfTest(result);
  result.exitCode = result.ok ? 0 : 1;
  if (!result.ok)
    result.message = "tests failed";
  return result;
}

