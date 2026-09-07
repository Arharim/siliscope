#include "siliscope/NoHeap.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CallExpr;
using clang::CXXDeleteExpr;
using clang::CXXNewExpr;
using clang::ast_matchers::callee;
using clang::ast_matchers::callExpr;
using clang::ast_matchers::cxxDeleteExpr;
using clang::ast_matchers::cxxNewExpr;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::hasAnyName;

void NoHeapCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(
      callExpr(callee(functionDecl(hasAnyName(
                   "malloc", "calloc", "realloc", "free", "aligned_alloc", "posix_memalign"))))
          .bind("call"),
      this);
  finder.addMatcher(cxxNewExpr().bind("new"), this);
  finder.addMatcher(cxxDeleteExpr().bind("delete"), this);
}

void NoHeapCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  clang::SourceLocation loc;
  if (const auto *c = result.Nodes.getNodeAs<CallExpr>("call")) {
    loc = c->getBeginLoc();
  } else if (const auto *n = result.Nodes.getNodeAs<CXXNewExpr>("new")) {
    loc = n->getBeginLoc();
  } else if (const auto *d = result.Nodes.getNodeAs<CXXDeleteExpr>("delete")) {
    loc = d->getBeginLoc();
  } else {
    return;
  }
  reporter.emit(
      *result.SourceManager, loc, "error", "ss.mem.no-heap-after-init", "do not use the heap");
}
