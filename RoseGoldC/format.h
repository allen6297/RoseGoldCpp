#pragma once

#include "ast.h"

#include <string>
#include <vector>

struct FormatOptions {
  // Blank line between top-level / mod items (except consecutive imports).
  bool blankBetweenItems = true;
  // Keep leading/trailing item comments and same-line stmt comments.
  bool keepComments = true;
};

struct FormatResult {
  bool ok = true;
  std::string out;
  std::string message;
  int exitCode = 0;
};

std::string formatProgram(const Program &program,
                          const FormatOptions &opts = {});
FormatResult formatSource(const std::string &source, const std::string &path,
                          const FormatOptions &opts = {});
FormatResult formatFile(const std::string &path,
                        const FormatOptions &opts = {});
