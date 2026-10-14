#include "siliscope/AggregateInit.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>

using clang::DesignatedInitExpr;
using clang::Expr;
using clang::InitListExpr;
using clang::QualType;
using clang::StringLiteral;
using clang::VarDecl;
using clang::ast_matchers::initListExpr;
using clang::ast_matchers::stringLiteral;
using clang::ast_matchers::varDecl;

namespace {

constexpr const char *kPartial = "initialize every element of this aggregate";
constexpr const char *kSize = "state the size of this designated array";
constexpr const char *kBraces = "enclose this nested aggregate in braces";
constexpr const char *kId = "ss.expr.aggregate-init";

const Expr *peel(const Expr *e) {
  return e ? e->IgnoreParenImpCasts() : nullptr;
}

bool isUnion(QualType type) {
  const auto *record = type.isNull() ? nullptr : type->getAs<clang::RecordType>();
  return record && record->getDecl()->isUnion();
}

bool isAggregate(QualType type) {
  if (type.isNull() || type->isDependentType()) {
    return false;
  }
  if (type->isArrayType()) {
    return true;
  }
  const auto *record = type->getAs<clang::RecordType>();
  return record && !record->getDecl()->isUnion();
}

bool levelHasHole(const InitListExpr *sem) {
  if (!sem || isUnion(sem->getType())) {
    return false;
  }
  // A short array keeps one written element and a filler for the tail.
  if (llvm::isa_and_nonnull<clang::ImplicitValueInitExpr>(peel(sem->getArrayFiller()))) {
    return true;
  }
  for (unsigned i = 0; i < sem->getNumInits(); ++i) {
    const Expr *init = peel(sem->getInit(i));
    if (llvm::isa_and_nonnull<clang::ImplicitValueInitExpr>(init)) {
      return true;
    }
  }
  return false;
}

QualType listType(const InitListExpr *syn) {
  if (!syn->getType().isNull()) {
    return syn->getType();
  }
  if (const InitListExpr *sem = syn->getSemanticForm()) {
    return sem->getType();
  }
  return {};
}

bool hasDesignated(const Expr *e) {
  e = peel(e);
  if (!e) {
    return false;
  }
  if (llvm::isa<DesignatedInitExpr>(e)) {
    return true;
  }
  const auto *list = llvm::dyn_cast<InitListExpr>(e);
  if (!list) {
    return false;
  }
  const InitListExpr *syn = list->isSyntacticForm() ? list : list->getSyntacticForm();
  if (!syn) {
    syn = list;
  }
  for (unsigned i = 0; i < syn->getNumInits(); ++i) {
    if (hasDesignated(syn->getInit(i))) {
      return true;
    }
  }
  return false;
}

const VarDecl *owningVar(const InitListExpr *syn, clang::ASTContext &ctx) {
  clang::DynTypedNode node = clang::DynTypedNode::create(*syn);
  for (int depth = 0; depth < 8; ++depth) {
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      return nullptr;
    }
    node = parents[0];
    if (node.get<clang::ParenExpr>() || node.get<clang::ImplicitCastExpr>() ||
        node.get<clang::ExprWithCleanups>()) {
      continue;
    }
    if (node.get<InitListExpr>() || node.get<DesignatedInitExpr>()) {
      return nullptr;
    }
    if (const auto *vd = node.get<VarDecl>()) {
      return vd;
    }
    if (const auto *literal = node.get<clang::CompoundLiteralExpr>()) {
      node = clang::DynTypedNode::create(*literal);
      continue;
    }
    return nullptr;
  }
  return nullptr;
}

bool allowed(const VarDecl *vd, Reporter &reporter) {
  if (!vd || vd->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = vd->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

void checkString(const StringLiteral *lit,
                 QualType type,
                 clang::SourceLocation loc,
                 Reporter &reporter,
                 const clang::SourceManager &sm,
                 clang::ASTContext &ctx) {
  const auto *array = ctx.getAsConstantArrayType(type);
  if (!lit || !array || !array->getElementType()->isAnyCharacterType()) {
    return;
  }
  const unsigned slots = array->getSize().getZExtValue();
  const unsigned chars = lit->getLength();
  if (slots > chars + 1) {
    reporter.emit(sm, loc, kId, kPartial);
  }
}

QualType elementType(QualType type, const DesignatedInitExpr *designated, clang::ASTContext &ctx) {
  if (designated) {
    for (unsigned i = 0; i < designated->size() && !type.isNull(); ++i) {
      const DesignatedInitExpr::Designator *design = designated->getDesignator(i);
      if (design->isFieldDesignator()) {
        const clang::FieldDecl *field = design->getFieldDecl();
        type = field ? field->getType() : QualType();
        continue;
      }
      const clang::ArrayType *array = ctx.getAsArrayType(type);
      type = array ? array->getElementType() : QualType();
    }
    return type;
  }
  if (const clang::ArrayType *array = ctx.getAsArrayType(type)) {
    return array->getElementType();
  }
  const auto *record = type.isNull() ? nullptr : type->getAs<clang::RecordType>();
  if (!record || record->getDecl()->isUnion()) {
    return type;
  }
  return {};
}

void checkNested(const Expr *init,
                 QualType slot,
                 Reporter &reporter,
                 const clang::SourceManager &sm,
                 clang::ASTContext &ctx) {
  init = peel(init);
  if (!init || slot.isNull() || !isAggregate(slot)) {
    return;
  }
  if (const auto *lit = llvm::dyn_cast<StringLiteral>(init)) {
    checkString(lit, slot, lit->getBeginLoc(), reporter, sm, ctx);
    return;
  }
  if (llvm::isa<InitListExpr>(init) || llvm::isa<DesignatedInitExpr>(init)) {
    return;
  }
  reporter.emit(sm, init->getExprLoc(), kId, kBraces);
}

} // namespace

void AggregateInitCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(initListExpr().bind("list"), this);
  finder.addMatcher(
      varDecl(clang::ast_matchers::hasInitializer(stringLiteral().bind("str"))).bind("var"), this);
}

void AggregateInitCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.SourceManager || !result.Context) {
    return;
  }
  clang::ASTContext &ctx = *result.Context;
  const clang::SourceManager &sm = *result.SourceManager;

  if (const auto *lit = result.Nodes.getNodeAs<StringLiteral>("str")) {
    const auto *vd = result.Nodes.getNodeAs<VarDecl>("var");
    if (!vd || allowed(vd, reporter)) {
      return;
    }
    checkString(lit, vd->getType(), lit->getBeginLoc(), reporter, sm, ctx);
    return;
  }

  const auto *matched = result.Nodes.getNodeAs<InitListExpr>("list");
  if (!matched) {
    return;
  }
  // The matcher visits the semantic form. The braces the user wrote live on
  // the syntactic form, which is not a child of the variable.
  if (matched->isSyntacticForm() && !matched->isSemanticForm() && matched->getSemanticForm()) {
    return;
  }
  const InitListExpr *sem = matched->isSemanticForm() ? matched : matched->getSemanticForm();
  const InitListExpr *syn = matched->isSyntacticForm() ? matched : matched->getSyntacticForm();
  if (!sem) {
    sem = matched;
  }
  if (!syn) {
    syn = matched;
  }
  if (syn->getBeginLoc().isInvalid()) {
    return;
  }
  const VarDecl *owner = owningVar(sem, ctx);
  if (!owner) {
    owner = owningVar(syn, ctx);
  }
  if (allowed(owner, reporter)) {
    return;
  }
  const QualType type = listType(sem);
  if (!type.isNull() && (type->isDependentType() || type->isVariableArrayType())) {
    return;
  }
  // An empty brace list value-initializes the whole aggregate. Its semantic
  // form is a run of implicit holes, and those are not partial initialization.
  if (syn->getNumInits() != 0 && levelHasHole(sem)) {
    reporter.emit(sm, syn->getBeginLoc(), kId, kPartial);
  }

  if (owner && owner->getTypeSourceInfo()) {
    const QualType written = owner->getTypeSourceInfo()->getType();
    if (written->isIncompleteArrayType() && hasDesignated(syn)) {
      reporter.emit(sm, syn->getBeginLoc(), kId, kSize);
    }
  }

  if (type.isNull() || type->isDependentType() || isUnion(type)) {
    return;
  }
  const auto *record = type->getAs<clang::RecordType>();
  const clang::FieldDecl *next = nullptr;
  if (record) {
    next = record->getDecl()->field_empty() ? nullptr : *record->getDecl()->field_begin();
    while (next && next->isUnnamedBitField()) {
      next = next->getNextDeclInContext()
                 ? llvm::dyn_cast<clang::FieldDecl>(next->getNextDeclInContext())
                 : nullptr;
    }
  }
  for (unsigned i = 0; i < syn->getNumInits(); ++i) {
    const Expr *raw = syn->getInit(i);
    const auto *designated = llvm::dyn_cast_or_null<DesignatedInitExpr>(peel(raw));
    QualType slot;
    if (designated) {
      slot = elementType(type, designated, ctx);
      checkNested(designated->getInit(), slot, reporter, sm, ctx);
      continue;
    }
    if (const clang::ArrayType *array = ctx.getAsArrayType(type)) {
      slot = array->getElementType();
    } else if (next) {
      slot = next->getType();
      const clang::Decl *step = next->getNextDeclInContext();
      next = nullptr;
      while (const auto *fieldDecl = llvm::dyn_cast_or_null<clang::FieldDecl>(step)) {
        if (!fieldDecl->isUnnamedBitField()) {
          next = fieldDecl;
          break;
        }
        step = fieldDecl->getNextDeclInContext();
      }
    }
    checkNested(raw, slot, reporter, sm, ctx);
  }
}
