#pragma once

#include "siliscope/Check.h"

// Marker so each style-profile id is a registered checker. The text and the
// AST are read once by attachStylePass.
class StyleCheck final : public Check {
public:
  using Check::Check;
  void registerMatchers(clang::ast_matchers::MatchFinder &finder) override;
  void run(const clang::ast_matchers::MatchFinder::MatchResult &result) override;
  bool wantsStyle() const override { return true; }
};

namespace clang {
class Preprocessor;
namespace ast_matchers {
class MatchFinder;
} // namespace ast_matchers
} // namespace clang

class Reporter;

void attachStylePass(clang::Preprocessor &pp,
                     Reporter &reporter,
                     clang::ast_matchers::MatchFinder &finder);
