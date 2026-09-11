#pragma once

#include "ast.h"

#include <string>

Program parseSource(const std::string& source, const std::string& file = "");
