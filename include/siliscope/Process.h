#pragma once

#include "siliscope/Check.h"

#include "llvm/ADT/StringRef.h"

#include <string>
#include <vector>

class ProcessCheck final : public Check {
public:
  using Check::Check;
  void registerMatchers(clang::ast_matchers::MatchFinder &finder) override;
  void run(const clang::ast_matchers::MatchFinder::MatchResult &result) override;
};

// The original compile command for one translation unit, before siliscope
// rewrites it for Clang. A direct file on the command line has none.
void rememberCompileCommand(llvm::StringRef file, const std::vector<std::string> &args);
