#include "siliscope/NoFpEq.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::BinaryOperator;
using clang::ast_matchers::binaryOperator;

void NoFpEqCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(binaryOperator().bind("cmp"), this);
}

void NoFpEqCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *cmp = result.Nodes.getNodeAs<BinaryOperator>("cmp");
  if (!cmp || !result.SourceManager) {
    return;
  }
  const auto op = cmp->getOpcode();
  if (op != clang::BO_EQ && op != clang::BO_NE) {
    return;
  }
  const bool left = cmp->getLHS()->getType()->isRealFloatingType();
  const bool right = cmp->getRHS()->getType()->isRealFloatingType();
  if (!left && !right) {
    return;
  }
  reporter.emit(*result.SourceManager,
                cmp->getOperatorLoc(),
                "ss.conv.no-fp-eq",
                "do not compare floating values with == or !=");
}
