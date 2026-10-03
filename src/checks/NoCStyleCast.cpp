#include "siliscope/NoCStyleCast.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CStyleCastExpr;
using clang::CXXFunctionalCastExpr;
using clang::ast_matchers::cStyleCastExpr;
using clang::ast_matchers::cxxFunctionalCastExpr;

void NoCStyleCastCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(cStyleCastExpr().bind("c"), this);
  finder.addMatcher(cxxFunctionalCastExpr().bind("fn"), this);
}

void NoCStyleCastCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  // strict enables this rule for a C translation unit too. A cast in C is
  // not the C++ rule.
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  clang::SourceLocation loc;
  if (const auto *c = result.Nodes.getNodeAs<CStyleCastExpr>("c")) {
    loc = c->getBeginLoc();
  } else if (const auto *fn = result.Nodes.getNodeAs<CXXFunctionalCastExpr>("fn")) {
    // T{...} is list-initialization, including aggregate Foo{}. The cast
    // this rule names is the parenthesized form, T(x).
    if (fn->isListInitialization()) {
      return;
    }
    loc = fn->getBeginLoc();
  } else {
    return;
  }
  reporter.emit(*result.SourceManager,
                loc,
                "ss.cpp.no-cstyle-cast",
                "use static_cast, const_cast, or reinterpret_cast");
}
