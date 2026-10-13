#include "siliscope/MaxIndirection.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::FieldDecl;
using clang::FunctionDecl;
using clang::ParmVarDecl;
using clang::QualType;
using clang::TypedefNameDecl;
using clang::VarDecl;
using clang::ast_matchers::fieldDecl;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::typedefNameDecl;
using clang::ast_matchers::unless;
using clang::ast_matchers::varDecl;

namespace {

bool tooDeep(QualType type, int depth, clang::ASTContext &ctx, int guard);

bool functionTooDeep(const clang::FunctionType *fn, clang::ASTContext &ctx, int guard) {
  if (tooDeep(fn->getReturnType(), 0, ctx, guard)) {
    return true;
  }
  const auto *proto = llvm::dyn_cast<clang::FunctionProtoType>(fn);
  if (!proto) {
    return false;
  }
  for (QualType param : proto->getParamTypes()) {
    if (tooDeep(param, 0, ctx, guard)) {
      return true;
    }
  }
  return false;
}

// Arrays and references do not add a level. A function type starts again at
// zero, so void (*fp)(int ***) is deep because of the parameter, not fp.
bool tooDeep(QualType type, int depth, clang::ASTContext &ctx, int guard) {
  while (!type.isNull() && guard <= 64) {
    ++guard;
    if (const auto *ptr = type->getAs<clang::PointerType>()) {
      ++depth;
      if (depth > 2) {
        return true;
      }
      type = ptr->getPointeeType();
      continue;
    }
    if (const clang::ArrayType *arr = ctx.getAsArrayType(type)) {
      type = arr->getElementType();
      continue;
    }
    if (type->isReferenceType()) {
      type = type->getPointeeType();
      continue;
    }
    if (const auto *mem = type->getAs<clang::MemberPointerType>()) {
      type = mem->getPointeeType();
      continue;
    }
    if (const auto *fn = type->getAs<clang::FunctionType>()) {
      return functionTooDeep(fn, ctx, guard);
    }
    const QualType next = type.getSingleStepDesugaredType(ctx);
    if (next.getTypePtr() == type.getTypePtr()) {
      return false;
    }
    type = next;
  }
  return false;
}

bool instantiated(const clang::Decl *d) {
  if (const auto *fn = llvm::dyn_cast<FunctionDecl>(d)) {
    return fn->isTemplateInstantiation();
  }
  if (const auto *vd = llvm::dyn_cast<VarDecl>(d)) {
    return vd->getTemplateSpecializationKind() == clang::TSK_ImplicitInstantiation;
  }
  if (const auto *fd = llvm::dyn_cast<FieldDecl>(d)) {
    if (const auto *rec = llvm::dyn_cast<clang::CXXRecordDecl>(fd->getParent())) {
      return rec->getTemplateSpecializationKind() == clang::TSK_ImplicitInstantiation;
    }
  }
  if (const auto *td = llvm::dyn_cast<TypedefNameDecl>(d)) {
    if (const auto *rec = llvm::dyn_cast<clang::CXXRecordDecl>(td->getDeclContext())) {
      return rec->getTemplateSpecializationKind() == clang::TSK_ImplicitInstantiation;
    }
  }
  return false;
}

void note(Reporter &reporter, const clang::SourceManager &sm, const clang::Decl *d) {
  if (!d || d->isImplicit() || instantiated(d)) {
    return;
  }
  reporter.emit(sm, d->getLocation(), "ss.ptr.max-indirection", "more than two pointer levels");
}

} // namespace

void MaxIndirectionCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(varDecl(unless(isImplicit())).bind("var"), this);
  finder.addMatcher(fieldDecl(unless(isImplicit())).bind("field"), this);
  finder.addMatcher(typedefNameDecl().bind("td"), this);
  finder.addMatcher(functionDecl(unless(isImplicit())).bind("fn"), this);
}

void MaxIndirectionCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.SourceManager) {
    return;
  }
  clang::ASTContext &ctx = *result.Context;
  const clang::SourceManager &sm = *result.SourceManager;
  if (const auto *vd = result.Nodes.getNodeAs<VarDecl>("var")) {
    QualType type = vd->getType();
    if (const auto *parm = llvm::dyn_cast<ParmVarDecl>(vd)) {
      type = parm->getOriginalType();
    }
    if (tooDeep(type, 0, ctx, 0)) {
      note(reporter, sm, vd);
    }
    return;
  }
  if (const auto *fd = result.Nodes.getNodeAs<FieldDecl>("field")) {
    if (tooDeep(fd->getType(), 0, ctx, 0)) {
      note(reporter, sm, fd);
    }
    return;
  }
  if (const auto *td = result.Nodes.getNodeAs<TypedefNameDecl>("td")) {
    if (tooDeep(td->getUnderlyingType(), 0, ctx, 0)) {
      note(reporter, sm, td);
    }
    return;
  }
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (fn && tooDeep(fn->getReturnType(), 0, ctx, 0)) {
    note(reporter, sm, fn);
  }
}
