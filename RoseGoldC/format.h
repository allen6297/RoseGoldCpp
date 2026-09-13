#pragma once

#include "ast.h"

#include <string>
#include <vector>

struct FormatResult {
  bool ok = true;
  std::string out;
  std::string message;
  int exitCode = 0;
};

std::string formatProgram(const Program &program);
FormatResult formatSource(const std::string &source, const std::string &path);
FormatResult formatFile(const std::string &path);
