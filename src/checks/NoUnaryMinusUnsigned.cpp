#include "siliscope/NoUnaryMinusUnsigned.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::Expr;
using clang::QualType;
using clang::UnaryOperator;
using clang::ast_matchers::hasOperatorName;
using clang::ast_matchers::unaryOperator;

namespace {

// Unary minus promotes first. unsigned char becomes int and stays quiet.
// unsigned int stays unsigned, and the negation is a wrap.
static bool negatesUnsigned(const Expr *operand, clang::ASTContext &ctx) {
  if (!operand || operand->isTypeDependent()) {
    return false;
  }
  QualType t = operand->IgnoreParenImpCasts()->getType().getCanonicalType().getUnqualifiedType();
  if (t.isNull() || !t->isIntegerType()) {
    return false;
  }
  if (const auto *en = t->getAs<clang::EnumType>()) {
    t = en->getDecl()->getIntegerType();
    if (t.isNull()) {
      return false;
    }
    t = t.getCanonicalType().getUnqualifiedType();
  }
  if (ctx.isPromotableIntegerType(t)) {
    t = ctx.getPromotedIntegerType(t);
  }
  return t->isUnsignedIntegerType();
}

} // namespace

void NoUnaryMinusUnsignedCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(unaryOperator(hasOperatorName("-")).bind("neg"), this);
}

void NoUnaryMinusUnsignedCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *neg = result.Nodes.getNodeAs<UnaryOperator>("neg");
  if (!neg || !result.Context || !result.SourceManager) {
    return;
  }
  if (!negatesUnsigned(neg->getSubExpr(), *result.Context)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                neg->getOperatorLoc(),
                "ss.conv.no-unary-minus-unsigned",
                "do not negate an unsigned value");
}
