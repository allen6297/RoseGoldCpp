#include "interp.h"
#include "parser.h"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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

