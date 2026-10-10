#include "siliscope/NoObjectPtrCast.h"

#include "siliscope/Report.h"

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CastExpr;
using clang::QualType;
using clang::ast_matchers::castExpr;

namespace {

static bool isExplicit(const CastExpr *cast) {
  return cast && !llvm::isa<clang::ImplicitCastExpr>(cast);
}

static QualType pointee(QualType pointer) {
  if (pointer.isNull() || !pointer->isPointerType()) {
    return {};
  }
  return pointer->getPointeeType().getCanonicalType().getUnqualifiedType();
}

static bool isByteOverlay(QualType t) {
  if (t.isNull()) {
    return false;
  }
  if (t->isAnyCharacterType()) {
    return true;
  }
  const clang::TagDecl *tag = t->getAsTagDecl();
  if (!tag || tag->getName() != "byte") {
    return false;
  }
  const auto *ns = llvm::dyn_cast<clang::NamespaceDecl>(tag->getDeclContext());
  return ns && ns->isStdNamespace();
}

static bool isPackedRecord(QualType t) {
  const auto *record = t.isNull() ? nullptr : t->getAs<clang::RecordType>();
  return record && record->getDecl()->hasAttr<clang::PackedAttr>();
}

static bool relatedRecords(QualType a, QualType b) {
  const auto *left = a.isNull() ? nullptr : a->getAsCXXRecordDecl();
  const auto *right = b.isNull() ? nullptr : b->getAsCXXRecordDecl();
  if (!left || !right || !left->hasDefinition() || !right->hasDefinition()) {
    return false;
  }
  return left->getCanonicalDecl() == right->getCanonicalDecl() || left->isDerivedFrom(right) ||
         right->isDerivedFrom(left);
}

} // namespace

void NoObjectPtrCastCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(castExpr().bind("cast"), this);
}

void NoObjectPtrCastCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *cast = result.Nodes.getNodeAs<CastExpr>("cast");
  if (!isExplicit(cast) || !cast->getSubExpr() || !result.SourceManager) {
    return;
  }
  if (cast->getCastKind() == clang::CK_NullToPointer || cast->getCastKind() == clang::CK_ToVoid ||
      cast->getCastKind() == clang::CK_DerivedToBase ||
      cast->getCastKind() == clang::CK_UncheckedDerivedToBase ||
      cast->getCastKind() == clang::CK_BaseToDerived) {
    return;
  }
  const QualType from = cast->getSubExpr()->IgnoreParenImpCasts()->getType();
  const QualType to = cast->getType();
  if (from->isFunctionType() || from->isFunctionPointerType() ||
      from->isMemberFunctionPointerType() || to->isFunctionPointerType() ||
      to->isMemberFunctionPointerType()) {
    return;
  }
  if (!from->isPointerType() || !to->isPointerType()) {
    return;
  }
  if (from->isVoidPointerType() || to->isVoidPointerType()) {
    return;
  }
  const QualType fromPointee = pointee(from);
  const QualType toPointee = pointee(to);
  if (fromPointee.isNull() || toPointee.isNull() || fromPointee == toPointee) {
    return;
  }
  if (isByteOverlay(fromPointee) || isByteOverlay(toPointee) || isPackedRecord(fromPointee) ||
      isPackedRecord(toPointee) || relatedRecords(fromPointee, toPointee)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                cast->getBeginLoc(),
                "ss.conv.no-object-ptr-cast",
                "do not cast between unrelated object pointers");
}
