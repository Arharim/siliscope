#include "siliscope/NoFnPtrCast.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CastExpr;
using clang::Expr;
using clang::QualType;
using clang::ast_matchers::castExpr;

namespace {

static QualType functionSignature(QualType t) {
  if (t.isNull()) {
    return {};
  }
  t = t.getCanonicalType().getUnqualifiedType();
  if (t->isFunctionPointerType()) {
    t = t->getPointeeType().getCanonicalType().getUnqualifiedType();
  }
  if (!t->isFunctionType()) {
    return {};
  }
  return t;
}

static bool nullConstant(const Expr *e, clang::ASTContext &ctx) {
  return e && e->IgnoreParenImpCasts()->isNullPointerConstant(
                  ctx, Expr::NPC_ValueDependentIsNotNull) != Expr::NPCK_NotNull;
}

static bool involvesFunctionPointer(QualType t) {
  return !t.isNull() &&
         (t->isFunctionType() || t->isFunctionPointerType() || t->isMemberFunctionPointerType());
}

} // namespace

void NoFnPtrCastCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(castExpr().bind("cast"), this);
}

void NoFnPtrCastCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *cast = result.Nodes.getNodeAs<CastExpr>("cast");
  if (!cast || !cast->getSubExpr() || llvm::isa<clang::ImplicitCastExpr>(cast) || !result.Context ||
      !result.SourceManager) {
    return;
  }
  // `(void)fp` discards the pointer. It does not give it another type.
  if (cast->getCastKind() == clang::CK_ToVoid) {
    return;
  }
  const QualType from = cast->getSubExpr()->IgnoreParenImpCasts()->getType();
  const QualType to = cast->getType();
  if (!involvesFunctionPointer(from) && !involvesFunctionPointer(to)) {
    return;
  }
  if (from->isMemberFunctionPointerType() || to->isMemberFunctionPointerType()) {
    if (from.getCanonicalType().getUnqualifiedType() ==
        to.getCanonicalType().getUnqualifiedType()) {
      return;
    }
  } else {
    const QualType fromSig = functionSignature(from);
    const QualType toSig = functionSignature(to);
    if (!fromSig.isNull() && fromSig == toSig) {
      return;
    }
  }
  if (involvesFunctionPointer(to) && nullConstant(cast->getSubExpr(), *result.Context)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                cast->getBeginLoc(),
                "ss.conv.no-fn-ptr-cast",
                "do not convert a function pointer to another type");
}
