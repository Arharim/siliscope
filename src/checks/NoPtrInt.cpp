#include "siliscope/NoPtrInt.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
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

static bool hasVolatileMember(const clang::RecordDecl *rec, int depth) {
  if (!rec || depth > 2) {
    return false;
  }
  for (const clang::FieldDecl *field : rec->fields()) {
    QualType ft = field->getType();
    if (const clang::ArrayType *array = ft->getAsArrayTypeUnsafe()) {
      ft = array->getElementType();
    }
    if (ft.isVolatileQualified()) {
      return true;
    }
    if (const auto *nested = ft->getAs<clang::RecordType>()) {
      if (hasVolatileMember(nested->getDecl(), depth + 1)) {
        return true;
      }
    }
  }
  return false;
}

// `*(volatile uint32_t *)0xE000EDFCu` and CMSIS `((RCC_TypeDef *)RCC_BASE)`.
// The address is a constant. A plain `(int *)0x1000u` is not a register block.
static bool isMmioPointer(QualType pointer) {
  if (!pointer->isPointerType()) {
    return false;
  }
  const QualType pointee = pointer->getPointeeType();
  if (pointee.isVolatileQualified()) {
    return true;
  }
  if (const auto *rec = pointee->getAs<clang::RecordType>()) {
    return hasVolatileMember(rec->getDecl(), 0);
  }
  return false;
}

static bool isAddressConstant(const Expr *e, clang::ASTContext &ctx) {
  if (!e) {
    return false;
  }
  clang::Expr::EvalResult value;
  return e->IgnoreParenImpCasts()->EvaluateAsInt(value, ctx) && value.Val.isInt();
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
  if (intToPointer && isNullPointerConstant(cast->getSubExpr(), *result.Context)) {
    return;
  }
  // A constant address written as a volatile pointer or a register struct is MMIO.
  // `(uintptr_t)p` and `(int *)runtime_value` stay flagged.
  if (intToPointer && isAddressConstant(cast->getSubExpr(), *result.Context) && isMmioPointer(to)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                cast->getBeginLoc(),
                "ss.conv.no-ptr-int",
                "do not convert between a pointer and an integer");
}
