#include "eval.h"
#include "format.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

std::string version = "0.0.1";

static bool command(const std::string &com, const std::string &name) {
  if (name.empty())
    return false;

  std::string letter(1, name[0]);

  return com == name || com == letter || com == "-" + letter ||
         com == "--" + letter;
}

static void printAliases(const std::string &name) {
  std::string letter(1, name[0]);
  std::cout << "  " << name << " : " << letter << ", -" << letter << ", --"
            << letter << "\n";
}

static std::vector<std::string> splitLine(const std::string &line) {
  std::vector<std::string> out;
  std::string cur;
  bool inQuote = false;

  for (unsigned char c : line) {
    if (c == '"') {
      inQuote = !inQuote;
      continue;
    }
    if (!inQuote && std::isspace(c)) {
      if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
    } else {
      cur.push_back(static_cast<char>(c));
    }
  }

  if (!cur.empty())
    out.push_back(cur);

  return out;
}

struct Invocation {
  std::string cmd;
  std::vector<std::string> args;
};

static Invocation parseArgv(int argc, char *argv[]) {
  Invocation inv;
  if (argc >= 2)
    inv.cmd = argv[1];
  for (int i = 2; i < argc; ++i)
    inv.args.emplace_back(argv[i]);
  return inv;
}

static Invocation parseLine(const std::string &line) {
  auto tokens = splitLine(line);
  Invocation inv;
  if (!tokens.empty()) {
    inv.cmd = tokens[0];
    inv.args.assign(tokens.begin() + 1, tokens.end());
  }
  return inv;
}

static void printRunResult(const RunResult &result) {
  std::cout << result.out;
  if (result.message.empty())
    return;
  if (!result.ok && result.out.find(result.message) == std::string::npos)
    std::cerr << result.message << "\n";
  else if (result.ok && result.out.empty())
    std::cerr << result.message << "\n";
}

struct DispatchResult {
  bool quit = false;
  int exitCode = 0;
};

static DispatchResult dispatch(const Invocation &inv) {
  DispatchResult result;
  if (inv.cmd.empty())
    return result;

  if (command(inv.cmd, "help")) {
    std::cout << "Commands:\n";
    printAliases("help");
    printAliases("quit");
    printAliases("run");
    std::cout << "      run <file> [args...]\n";
    printAliases("check");
    std::cout << "      check [--json] [--stdin] <file>\n";
    printAliases("fmt");
    std::cout
        << "      fmt [--write|-w] [--check] [--compact] [--no-comments] "
           "<file>\n";
    printAliases("lsp");
    std::cout << "      lsp             language server (stdin/stdout JSON-RPC)\n";
    printAliases("dap");
    std::cout << "      dap             debug adapter (stdin/stdout DAP)\n";
    printAliases("test");
    std::cout << "      test            language suite\n";
    std::cout << "      test <file>     @test functions in one file\n";
    std::cout << "      test <dir>      pass/ and fail/ under dir\n";
    printAliases("version");
    return result;
  }
  if (command(inv.cmd, "quit") || inv.cmd == "exit") {
    std::cout << "Goodbye\n";
    result.quit = true;
    return result;
  }
  if (command(inv.cmd, "run")) {
    if (inv.args.empty()) {
      std::cerr << "Usage: run <file> [args...]\n";
      result.exitCode = 2;
      return result;
    }
    RunResult run = runFile(inv.args[0], inv.args);
    printRunResult(run);
    result.exitCode = run.exitCode;
    return result;
  }
  if (command(inv.cmd, "check")) {
    bool json = false;
    bool fromStdin = false;
    std::string path;
    for (const auto &a : inv.args) {
      if (a == "--json" || a == "-j")
        json = true;
      else if (a == "--stdin")
        fromStdin = true;
      else if (path.empty())
        path = a;
    }
    if (path.empty()) {
      std::cerr << "Usage: check [--json] [--stdin] <file>\n";
      result.exitCode = 2;
      return result;
    }
    std::vector<Diagnostic> diags;
    if (fromStdin) {
      std::ostringstream ss;
      ss << std::cin.rdbuf();
      diags = checkSource(ss.str(), path);
    } else {
      diags = checkFile(path);
    }
    if (json)
      std::cout << diagnosticsToJson(diags);
    else {
      for (const auto &d : diags)
        std::cerr << diagnosticToHuman(d) << "\n";
    }
    result.exitCode = diags.empty() ? 0 : 1;
    return result;
  }
  if (command(inv.cmd, "fmt")) {
    bool write = false;
    bool checkOnly = false;
    FormatOptions opts;
    std::string path;
    for (const auto &a : inv.args) {
      if (a == "--write" || a == "-w")
        write = true;
      else if (a == "--check")
        checkOnly = true;
      else if (a == "--compact")
        opts.blankBetweenItems = false;
      else if (a == "--no-comments")
        opts.keepComments = false;
      else if (path.empty())
        path = a;
    }
    if (path.empty()) {
      std::cerr << "Usage: fmt [--write|-w] [--check] [--compact] "
                   "[--no-comments] <file>\n";
      result.exitCode = 2;
      return result;
    }
    std::ifstream in(path);
    if (!in) {
      std::cerr << "cannot open " << path << "\n";
      result.exitCode = 2;
      return result;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string original = ss.str();
    FormatResult fmt = formatSource(original, path, opts);
    if (!fmt.ok) {
      std::cerr << path << ": " << fmt.message << "\n";
      result.exitCode = fmt.exitCode ? fmt.exitCode : 1;
      return result;
    }
    if (checkOnly) {
      if (fmt.out != original) {
        std::cerr << path << " needs formatting\n";
        result.exitCode = 1;
      }
      return result;
    }
    if (write) {
      if (fmt.out != original) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
          std::cerr << "cannot write " << path << "\n";
          result.exitCode = 2;
          return result;
        }
        out << fmt.out;
      }
      return result;
    }
    std::cout << fmt.out;
    return result;
  }
  if (command(inv.cmd, "lsp")) {
    result.exitCode = runLanguageServer();
    return result;
  }
  if (command(inv.cmd, "dap")) {
    result.exitCode = runDebugAdapter();
    return result;
  }
  if (command(inv.cmd, "test")) {
    RunResult run;
    if (inv.args.empty()) {
      run = testLanguage();
    } else if (inv.args.size() == 1) {
      std::error_code ec;
      if (std::filesystem::is_directory(inv.args[0], ec))
        run = testSuite(inv.args[0]);
      else
        run = testFile(inv.args[0]);
    } else {
      std::cerr << "Usage: test [file|dir]\n";
      result.exitCode = 2;
      return result;
    }
    printRunResult(run);
    result.exitCode = run.exitCode;
    return result;
  }
  if (command(inv.cmd, "version")) {
    std::cout << "version : " << version << std::endl;
    return result;
  }

  std::cout << "Unknown command: " << inv.cmd << "\n";
  result.exitCode = 2;
  return result;
}

int main(int argc, char *argv[]) {
  if (argc >= 2)
    return dispatch(parseArgv(argc, argv)).exitCode;

  std::cout << "Type a command (help, quit)\n";
  std::string line;
  while (true) {
    std::cout << "rosegold ";
    if (!std::getline(std::cin, line))
      break;
    DispatchResult result = dispatch(parseLine(line));
    if (result.quit)
      break;
  }
}
