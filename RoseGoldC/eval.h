#pragma once

#include <string>
#include <vector>

struct RunResult {
  bool ok = true;
  std::string out;
  std::string message;
  int exitCode = 0;
};

RunResult runFile(const std::string &path,
                  const std::vector<std::string> &argv = {});
RunResult testFile(const std::string &path);
RunResult testSuite(const std::string &root);
RunResult testLanguage();
