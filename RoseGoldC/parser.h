#pragma once

#include "ast.h"
#include "lexer.h"

#include <string>
#include <vector>

Program parseSource(const std::string &source, const std::string &file = "",
                    std::vector<Diagnostic> *errors = nullptr);
