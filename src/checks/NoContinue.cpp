#include "siliscope/NoContinue.h"

#include "siliscope/Report.h"

#include "clang/ASTMatchers/ASTMatchers.h"

using clang::ContinueStmt;
using clang::ast_matchers::continueStmt;

void NoContinueCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(continueStmt().bind("continue"), this);
}

void NoContinueCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *s = result.Nodes.getNodeAs<ContinueStmt>("continue");
  if (!s) {
    return;
  }
  reporter.emit(
      *result.SourceManager, s->getContinueLoc(), "ss.ctrl.no-continue", "do not use continue");
}
