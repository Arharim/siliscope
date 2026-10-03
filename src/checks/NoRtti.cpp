#include "siliscope/NoRtti.h"

#include "siliscope/Report.h"

#include "clang/AST/ExprCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchersMacros.h"

using clang::CXXDynamicCastExpr;
using clang::CXXTypeidExpr;
using clang::ast_matchers::cxxDynamicCastExpr;
using clang::ast_matchers::expr;

namespace {

// Clang 22 has no cxxTypeidExpr() matcher.
AST_MATCHER(clang::Expr, isTypeidExpr) {
  return llvm::isa<clang::CXXTypeidExpr>(Node);
}

} // namespace

void NoRttiCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(cxxDynamicCastExpr().bind("dyn"), this);
  finder.addMatcher(expr(isTypeidExpr()).bind("typeid"), this);
}

void NoRttiCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  clang::SourceLocation loc;
  if (const auto *dyn = result.Nodes.getNodeAs<CXXDynamicCastExpr>("dyn")) {
    loc = dyn->getBeginLoc();
  } else if (const auto *tid = result.Nodes.getNodeAs<CXXTypeidExpr>("typeid")) {
    loc = tid->getBeginLoc();
  } else {
    return;
  }
  reporter.emit(*result.SourceManager, loc, "ss.cpp.no-rtti", "do not use RTTI");
}
