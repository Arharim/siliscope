#include "siliscope/NoSilentNarrow.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::Expr;
using clang::ImplicitCastExpr;
using clang::QualType;
using clang::ast_matchers::hasCastKind;
using clang::ast_matchers::implicitCastExpr;

namespace {

static bool isNarrowingInteger(QualType from, QualType to, clang::ASTContext &ctx) {
  if (from.isNull() || to.isNull()) {
    return false;
  }
  if (!from->isIntegerType() || !to->isIntegerType()) {
    return false;
  }
  if (from->isBooleanType() || to->isBooleanType()) {
    return false;
  }
  return ctx.getIntWidth(to) < ctx.getIntWidth(from);
}

// A constant that still fits keeps its value. The rule asks for a cast when
// the source is wider and the value is not known to fit.
static bool constantFits(const Expr *e, clang::ASTContext &ctx, QualType dest) {
  clang::Expr::EvalResult value;
  if (!e->EvaluateAsInt(value, ctx) || !value.Val.isInt()) {
    return false;
  }
  const llvm::APSInt v = value.Val.getInt();
  const unsigned bits = ctx.getIntWidth(dest);
  if (dest->isUnsignedIntegerType()) {
    return !v.isNegative() && v.getActiveBits() <= bits;
  }
  return v.getSignificantBits() <= bits;
}

} // namespace

void NoSilentNarrowCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(implicitCastExpr(hasCastKind(clang::CK_IntegralCast)).bind("cast"), this);
}

void NoSilentNarrowCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *cast = result.Nodes.getNodeAs<ImplicitCastExpr>("cast");
  if (!cast || !cast->getSubExpr() || !result.Context || !result.SourceManager) {
    return;
  }
  const QualType from = cast->getSubExpr()->getType();
  const QualType to = cast->getType();
  if (!isNarrowingInteger(from, to, *result.Context)) {
    return;
  }
  if (constantFits(cast->getSubExpr(), *result.Context, to)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                cast->getBeginLoc(),
                "ss.conv.no-silent-narrow",
                "implicit narrowing of an integer");
}
