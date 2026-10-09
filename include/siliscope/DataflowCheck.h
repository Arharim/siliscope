#pragma once

#include "siliscope/Check.h"

#include <memory>

// Marker so each dataflow id is a registered checker. The CFG walk runs once.
class DataflowCheck final : public Check {
public:
  using Check::Check;
  void registerMatchers(clang::ast_matchers::MatchFinder &finder) override;
  void run(const clang::ast_matchers::MatchFinder::MatchResult &result) override;
  bool wantsDataflow() const override { return true; }
};

namespace clang {
namespace ast_matchers {
class MatchFinder;
}
} // namespace clang

class ProgramFacts;

void attachDataflowPass(clang::ast_matchers::MatchFinder &finder,
                        Reporter &reporter,
                        std::unique_ptr<clang::ast_matchers::MatchFinder::MatchCallback> &slot,
                        ProgramFacts &facts);
