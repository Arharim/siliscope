#pragma once

#include "clang/ASTMatchers/ASTMatchFinder.h"

#include <memory>

class Reporter;

namespace clang {
class SourceManager;
class TranslationUnitDecl;
} // namespace clang

// Direct calls of one siliscope run. FunctionDecl pointers do not survive
// the translation unit, so nodes are USRs. A file-scope static keeps the
// file in its key: the same name in two .c files is two functions.
class ProgramCallGraph {
public:
  ProgramCallGraph();
  ~ProgramCallGraph();
  ProgramCallGraph(const ProgramCallGraph &) = delete;
  ProgramCallGraph &operator=(const ProgramCallGraph &) = delete;

  void record(clang::TranslationUnitDecl *tu, const clang::SourceManager &sm, Reporter &reporter);
  void finish(Reporter &reporter) const;

private:
  struct Data;
  std::unique_ptr<Data> data;
};

void attachCallGraph(clang::ast_matchers::MatchFinder &finder,
                     ProgramCallGraph &graph,
                     Reporter &reporter,
                     std::unique_ptr<clang::ast_matchers::MatchFinder::MatchCallback> &slot);
