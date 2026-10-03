#pragma once

#include "siliscope/Check.h"

// Marker so each ss.pre.* id is a registered checker. The directives
// themselves are read once by attachPreprocessorPass.
class PreprocessorCheck final : public Check {
public:
  using Check::Check;
  void registerMatchers(clang::ast_matchers::MatchFinder &finder) override;
  void run(const clang::ast_matchers::MatchFinder::MatchResult &result) override;
  bool wantsPreprocessor() const override { return true; }
};

namespace clang {
class Preprocessor;
namespace ast_matchers {
class MatchFinder;
}
} // namespace clang

void attachPreprocessorPass(clang::Preprocessor &pp,
                            Reporter &reporter,
                            clang::ast_matchers::MatchFinder &finder);
