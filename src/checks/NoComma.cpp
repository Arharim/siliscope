#include "siliscope/NoComma.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::BinaryOperator;
using clang::ast_matchers::binaryOperator;
using clang::ast_matchers::hasOperatorName;

void NoCommaCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(binaryOperator(hasOperatorName(",")).bind("comma"), this);
}

void NoCommaCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *op = result.Nodes.getNodeAs<BinaryOperator>("comma");
  if (!op) {
    return;
  }
  reporter.emit(*result.SourceManager,
                op->getOperatorLoc(),
                "ss.expr.no-comma",
                "do not use the comma operator");
}
