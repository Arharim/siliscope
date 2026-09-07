#pragma once

#include "clang/ASTMatchers/ASTMatchFinder.h"

class Reporter;

class Check : public clang::ast_matchers::MatchFinder::MatchCallback {
public:
  explicit Check(Reporter &r) : reporter(r) {}
  virtual ~Check() = default;
  virtual void registerMatchers(clang::ast_matchers::MatchFinder &finder) = 0;

protected:
  Reporter &reporter;
};
