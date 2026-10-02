#include "siliscope/NoPtrInt.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CastExpr;
using clang::Expr;
using clang::QualType;
using clang::ast_matchers::castExpr;

namespace {

static bool isObjectPointer(QualType t) {
  return !t.isNull() && t->isPointerType() && !t->isFunctionPointerType();
}

static bool isIntegerNotBool(QualType t) {
  return !t.isNull() && t->isIntegerType() && !t->isBooleanType();
}

static bool isNullPointerConstant(const Expr *e, clang::ASTContext &ctx) {
  if (!e) {
    return false;
  }
  return e->IgnoreParenImpCasts()->isNullPointerConstant(ctx, Expr::NPC_ValueDependentIsNotNull) !=
         Expr::NPCK_NotNull;
}

} // namespace

void NoPtrIntCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(castExpr().bind("cast"), this);
}

void NoPtrIntCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *cast = result.Nodes.getNodeAs<CastExpr>("cast");
  if (!cast || !cast->getSubExpr() || !result.Context || !result.SourceManager) {
    return;
  }
  if (cast->getCastKind() == clang::CK_PointerToBoolean ||
      cast->getCastKind() == clang::CK_NullToPointer) {
    return;
  }
  const QualType from = cast->getSubExpr()->getType();
  const QualType to = cast->getType();
  const bool pointerToInt = isObjectPointer(from) && isIntegerNotBool(to);
  const bool intToPointer = isIntegerNotBool(from) && isObjectPointer(to);
  if (!pointerToInt && !intToPointer) {
    return;
  }
  // NULL / 0 / 0u as a pointer is a null constant, not an address cast.
  // A HAL allowlist for MMIO is still a catalog note, not a config knob.
  if (intToPointer && isNullPointerConstant(cast->getSubExpr(), *result.Context)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                cast->getBeginLoc(),
                "ss.conv.no-ptr-int",
                "do not convert between a pointer and an integer");
}
