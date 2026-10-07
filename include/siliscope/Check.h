#pragma once

#include "clang/ASTMatchers/ASTMatchFinder.h"

class Reporter;

class Check : public clang::ast_matchers::MatchFinder::MatchCallback {
public:
  explicit Check(Reporter &r) : reporter(r) {}
  virtual ~Check() = default;
  virtual void registerMatchers(clang::ast_matchers::MatchFinder &finder) = 0;
  // Preprocessor rules share one PPCallbacks pass instead of an AST matcher.
  virtual bool wantsPreprocessor() const { return false; }
  // The four intra-TU dataflow rules share one CFG walk.
  virtual bool wantsDataflow() const { return false; }

protected:
  Reporter &reporter;
};
