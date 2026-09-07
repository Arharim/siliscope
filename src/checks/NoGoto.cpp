#include "siliscope/NoGoto.h"

#include "siliscope/Report.h"

#include "clang/ASTMatchers/ASTMatchers.h"

using clang::GotoStmt;
using clang::ast_matchers::gotoStmt;

void NoGotoCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(gotoStmt().bind("goto"), this);
}

void NoGotoCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *g = result.Nodes.getNodeAs<GotoStmt>("goto");
  if (!g) {
    return;
  }
  reporter.emit(*result.SourceManager, g->getGotoLoc(), "warning", "ss.ctrl.no-goto",
                "do not use goto");
}
