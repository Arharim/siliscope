#include "siliscope/SignedUnsignedMix.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::BinaryOperator;
using clang::ConditionalOperator;
using clang::Expr;
using clang::QualType;
using clang::ast_matchers::binaryOperator;
using clang::ast_matchers::conditionalOperator;

namespace {

static bool arithmeticMix(clang::BinaryOperator::Opcode op) {
  switch (op) {
  case clang::BO_Mul:
  case clang::BO_Div:
  case clang::BO_Rem:
  case clang::BO_Add:
  case clang::BO_Sub:
  case clang::BO_LT:
  case clang::BO_GT:
  case clang::BO_LE:
  case clang::BO_GE:
  case clang::BO_EQ:
  case clang::BO_NE:
  case clang::BO_And:
  case clang::BO_Xor:
  case clang::BO_Or:
  case clang::BO_MulAssign:
  case clang::BO_DivAssign:
  case clang::BO_RemAssign:
  case clang::BO_AddAssign:
  case clang::BO_SubAssign:
  case clang::BO_AndAssign:
  case clang::BO_XorAssign:
  case clang::BO_OrAssign:
    return true;
  default:
    return false;
  }
}

// Integer promotions, then the usual arithmetic conversions. A narrow unsigned
// value that becomes signed int is not a mix with another int. A bit-field
// narrower than int promotes to int, because int can hold every value.
static QualType promotedInteger(const Expr *e, clang::ASTContext &ctx) {
  if (!e || e->isTypeDependent()) {
    return {};
  }
  const Expr *inner = e->IgnoreParenImpCasts();
  if (const auto *member = llvm::dyn_cast<clang::MemberExpr>(inner)) {
    if (const auto *field = llvm::dyn_cast<clang::FieldDecl>(member->getMemberDecl())) {
      const Expr *width = field->getBitWidth();
      if (field->isBitField() && width && !width->isValueDependent() &&
          field->getBitWidthValue() < static_cast<unsigned>(ctx.getIntWidth(ctx.IntTy))) {
        return ctx.IntTy;
      }
    }
  }
  QualType t = inner->getType().getCanonicalType().getUnqualifiedType();
  if (t.isNull() || !t->isIntegerType()) {
    return {};
  }
  if (const auto *en = t->getAs<clang::EnumType>()) {
    QualType underlying = en->getDecl()->getIntegerType();
    if (underlying.isNull()) {
      return {};
    }
    t = underlying.getCanonicalType().getUnqualifiedType();
  }
  if (ctx.isPromotableIntegerType(t)) {
    t = ctx.getPromotedIntegerType(t);
  }
  if (!t->isSignedIntegerType() && !t->isUnsignedIntegerType()) {
    return {};
  }
  return t;
}

static bool mixes(const Expr *lhs, const Expr *rhs, clang::ASTContext &ctx) {
  const QualType left = promotedInteger(lhs, ctx);
  const QualType right = promotedInteger(rhs, ctx);
  if (left.isNull() || right.isNull()) {
    return false;
  }
  return left->isUnsignedIntegerType() != right->isUnsignedIntegerType();
}

} // namespace

void SignedUnsignedMixCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(binaryOperator().bind("bin"), this);
  finder.addMatcher(conditionalOperator().bind("cond"), this);
}

void SignedUnsignedMixCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.SourceManager) {
    return;
  }
  if (const auto *bin = result.Nodes.getNodeAs<BinaryOperator>("bin")) {
    if (!arithmeticMix(bin->getOpcode()) || !mixes(bin->getLHS(), bin->getRHS(), *result.Context)) {
      return;
    }
    reporter.emit(*result.SourceManager,
                  bin->getOperatorLoc(),
                  "ss.conv.signed-unsigned-mix",
                  "signed and unsigned integers meet in this expression");
    return;
  }
  const auto *cond = result.Nodes.getNodeAs<ConditionalOperator>("cond");
  if (!cond || !mixes(cond->getTrueExpr(), cond->getFalseExpr(), *result.Context)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                cond->getQuestionLoc(),
                "ss.conv.signed-unsigned-mix",
                "signed and unsigned integers meet in this expression");
}
