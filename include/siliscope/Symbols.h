#pragma once

#include "siliscope/Check.h"

#include "clang/ASTMatchers/ASTMatchFinder.h"

#include <memory>

class Reporter;

namespace clang {
class SourceManager;
class TranslationUnitDecl;
} // namespace clang

// External definitions, header declarations, parameter names, and extern
// object declarations for one siliscope run. Declaration pointers do not
// survive the translation unit, so finish() reports from saved sites.
class ProgramSymbols {
public:
  ProgramSymbols();
  ~ProgramSymbols();
  ProgramSymbols(const ProgramSymbols &) = delete;
  ProgramSymbols &operator=(const ProgramSymbols &) = delete;

  void record(clang::TranslationUnitDecl *tu, const clang::SourceManager &sm, Reporter &reporter);
  void finish(Reporter &reporter) const;

private:
  struct Data;
  std::unique_ptr<Data> data;
};

class SymbolCheck final : public Check {
public:
  using Check::Check;
  void registerMatchers(clang::ast_matchers::MatchFinder &finder) override;
  void run(const clang::ast_matchers::MatchFinder::MatchResult &result) override;
  bool wantsSymbols() const override { return true; }
};

void attachSymbols(clang::ast_matchers::MatchFinder &finder,
                   ProgramSymbols &symbols,
                   Reporter &reporter,
                   std::unique_ptr<clang::ast_matchers::MatchFinder::MatchCallback> &slot);
