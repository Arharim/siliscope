#include "siliscope/NoExceptions.h"

#include "siliscope/Report.h"

#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CXXThrowExpr;
using clang::CXXTryStmt;
using clang::ast_matchers::cxxThrowExpr;
using clang::ast_matchers::cxxTryStmt;

void NoExceptionsCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(cxxThrowExpr().bind("throw"), this);
  finder.addMatcher(cxxTryStmt().bind("try"), this);
}

void NoExceptionsCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  clang::SourceLocation loc;
  if (const auto *thr = result.Nodes.getNodeAs<CXXThrowExpr>("throw")) {
    loc = thr->getThrowLoc();
  } else if (const auto *tr = result.Nodes.getNodeAs<CXXTryStmt>("try")) {
    loc = tr->getBeginLoc();
  } else {
    return;
  }
  reporter.emit(*result.SourceManager, loc, "ss.cpp.no-exceptions", "do not use exceptions");
}
