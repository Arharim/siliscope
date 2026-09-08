#include "siliscope/NoLogicalRhs.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::BinaryOperator;
using clang::ast_matchers::anyOf;
using clang::ast_matchers::binaryOperator;
using clang::ast_matchers::callExpr;
using clang::ast_matchers::expr;
using clang::ast_matchers::hasDescendant;
using clang::ast_matchers::hasOperatorName;
using clang::ast_matchers::hasRHS;
using clang::ast_matchers::isAssignmentOperator;
using clang::ast_matchers::unaryOperator;

void NoLogicalRhsCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  auto rhs = expr(
      anyOf(callExpr(),
            hasDescendant(callExpr()),
            binaryOperator(isAssignmentOperator()),
            hasDescendant(binaryOperator(isAssignmentOperator())),
            unaryOperator(anyOf(hasOperatorName("++"), hasOperatorName("--"))),
            hasDescendant(unaryOperator(anyOf(hasOperatorName("++"), hasOperatorName("--"))))));
  finder.addMatcher(
      binaryOperator(anyOf(hasOperatorName("&&"), hasOperatorName("||")), hasRHS(rhs)).bind("op"),
      this);
}

void NoLogicalRhsCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *op = result.Nodes.getNodeAs<BinaryOperator>("op");
  if (!op) {
    return;
  }
  reporter.emit(*result.SourceManager,
                op->getOperatorLoc(),
                "ss.expr.no-logical-rhs-side-effect",
                "do not call or assign on the RHS of && or ||");
}
