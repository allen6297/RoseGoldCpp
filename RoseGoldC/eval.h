#pragma once

#include "lexer.h"

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
std::vector<Diagnostic> checkSource(const std::string &source,
                                    const std::string &path);
std::vector<Diagnostic> checkFile(const std::string &path);
std::string diagnosticsToJson(const std::vector<Diagnostic> &diags);
std::string diagnosticToHuman(const Diagnostic &d);
int runLanguageServer();
int runDebugAdapter();
bool jsonRpcSelfTest();
