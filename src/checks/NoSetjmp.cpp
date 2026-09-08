#include "siliscope/NoSetjmp.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CallExpr;
using clang::ast_matchers::callee;
using clang::ast_matchers::callExpr;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::hasAnyName;

void NoSetjmpCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(callExpr(callee(functionDecl(hasAnyName("setjmp",
                                                            "longjmp",
                                                            "_setjmp",
                                                            "_longjmp",
                                                            "sigsetjmp",
                                                            "siglongjmp",
                                                            "__sigsetjmp",
                                                            "__builtin_setjmp",
                                                            "__builtin_longjmp"))))
                        .bind("call"),
                    this);
}

void NoSetjmpCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *c = result.Nodes.getNodeAs<CallExpr>("call");
  if (!c) {
    return;
  }
  reporter.emit(
      *result.SourceManager, c->getBeginLoc(), "ss.ctrl.no-setjmp", "do not use setjmp/longjmp");
}
