#include "eval.h"

#include <cctype>
#include <filesystem>
#include <iostream>
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
