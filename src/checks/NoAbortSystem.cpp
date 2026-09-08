#include "siliscope/NoAbortSystem.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CallExpr;
using clang::ast_matchers::callee;
using clang::ast_matchers::callExpr;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::hasAnyName;

void NoAbortSystemCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(
      callExpr(callee(functionDecl(hasAnyName("abort", "exit", "getenv", "system")))).bind("call"),
      this);
}

void NoAbortSystemCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *c = result.Nodes.getNodeAs<CallExpr>("call");
  if (!c) {
    return;
  }
  reporter.emit(*result.SourceManager,
                c->getBeginLoc(),
                "ss.libc.no-abort-system",
                "do not use abort/exit/getenv/system");
}
