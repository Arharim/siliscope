#include "siliscope/NoSignal.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CallExpr;
using clang::ast_matchers::callee;
using clang::ast_matchers::callExpr;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::hasAnyName;

void NoSignalCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(
      callExpr(callee(functionDecl(hasAnyName("signal", "raise", "sigaction")))).bind("call"),
      this);
}

void NoSignalCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *c = result.Nodes.getNodeAs<CallExpr>("call");
  if (!c) {
    return;
  }
  reporter.emit(
      *result.SourceManager, c->getBeginLoc(), "ss.libc.no-signal", "do not use signal.h APIs");
}
