#include "siliscope/NoThrowDtor.h"

#include "siliscope/Report.h"

#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/ExceptionSpecificationType.h"

using clang::CXXDestructorDecl;
using clang::CXXRecordDecl;
using clang::CXXThrowExpr;
using clang::ExceptionSpecificationType;
using clang::FunctionProtoType;
using clang::QualType;
using clang::SourceLocation;
using clang::ast_matchers::cxxDestructorDecl;
using clang::ast_matchers::cxxThrowExpr;
using clang::ast_matchers::hasAncestor;

namespace {

bool subobjectsCanThrow(const CXXRecordDecl *rec, int depth);
bool dtorCanThrow(const CXXDestructorDecl *dtor, int depth);

// Clang leaves a defaulted destructor's exception spec unevaluated, and
// isNothrow() treats that as can-throw. Only an explicit throwing spec is
// decided here; a defaulted or implicit one follows its subobjects.
bool explicitSpecCanThrow(const FunctionProtoType *proto) {
  const ExceptionSpecificationType est = proto->getExceptionSpecType();
  if (est == clang::EST_NoexceptFalse || est == clang::EST_MSAny) {
    return true;
  }
  return est == clang::EST_Dynamic && proto->getNumExceptions() > 0;
}

bool specUnresolved(const FunctionProtoType *proto) {
  switch (proto->getExceptionSpecType()) {
  case clang::EST_None:
  case clang::EST_Unevaluated:
  case clang::EST_Uninstantiated:
  case clang::EST_DependentNoexcept:
  case clang::EST_Unparsed:
    return true;
  default:
    return false;
  }
}

bool typeCanThrow(QualType type, int depth) {
  if (type.isNull() || depth > 16 || type->isDependentType()) {
    return false;
  }
  for (unsigned n = 0; type->isArrayType() && n < 8; ++n) {
    const clang::ArrayType *array = type->getAsArrayTypeUnsafe();
    if (!array) {
      break;
    }
    type = array->getElementType();
  }
  if (type->isReferenceType() || !type->isRecordType()) {
    return false;
  }
  const auto *rec = type->getAsCXXRecordDecl();
  const CXXRecordDecl *def = rec ? rec->getDefinition() : nullptr;
  if (!def) {
    return false;
  }
  if (const CXXDestructorDecl *dtor = def->getDestructor()) {
    return dtorCanThrow(dtor, depth + 1);
  }
  return subobjectsCanThrow(def, depth + 1);
}

bool subobjectsCanThrow(const CXXRecordDecl *rec, int depth) {
  const CXXRecordDecl *def = rec ? rec->getDefinition() : nullptr;
  if (!def || depth > 16) {
    return false;
  }
  for (const clang::FieldDecl *field : def->fields()) {
    if (typeCanThrow(field->getType(), depth)) {
      return true;
    }
  }
  for (const clang::CXXBaseSpecifier &base : def->bases()) {
    if (typeCanThrow(base.getType(), depth)) {
      return true;
    }
  }
  for (const clang::CXXBaseSpecifier &base : def->vbases()) {
    if (typeCanThrow(base.getType(), depth)) {
      return true;
    }
  }
  return false;
}

bool dtorCanThrow(const CXXDestructorDecl *dtor, int depth) {
  if (!dtor || depth > 16) {
    return false;
  }
  const auto *proto = dtor->getType()->getAs<FunctionProtoType>();
  if (!proto) {
    return false;
  }
  if (explicitSpecCanThrow(proto)) {
    return true;
  }
  if (!specUnresolved(proto) || (!dtor->isDefaulted() && !dtor->isImplicit())) {
    return false;
  }
  return subobjectsCanThrow(dtor->getParent(), depth + 1);
}

SourceLocation reportLoc(const CXXDestructorDecl *dtor) {
  if (!dtor->isImplicit() && dtor->getLocation().isValid()) {
    return dtor->getLocation();
  }
  if (const auto *rec = dtor->getParent()) {
    if (rec->getLocation().isValid()) {
      return rec->getLocation();
    }
  }
  return dtor->getLocation();
}

} // namespace

void NoThrowDtorCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(cxxDestructorDecl().bind("dtor"), this);
  finder.addMatcher(cxxThrowExpr(hasAncestor(cxxDestructorDecl())).bind("throw"), this);
}

void NoThrowDtorCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  if (const auto *thr = result.Nodes.getNodeAs<CXXThrowExpr>("throw")) {
    reporter.emit(*result.SourceManager,
                  thr->getThrowLoc(),
                  "ss.cpp.no-throw-dtor",
                  "a destructor shall not throw");
    return;
  }
  const auto *dtor = result.Nodes.getNodeAs<CXXDestructorDecl>("dtor");
  if (!dtor || dtor != dtor->getCanonicalDecl() || dtor->isTemplateInstantiation() ||
      !dtorCanThrow(dtor, 0)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                reportLoc(dtor),
                "ss.cpp.no-throw-dtor",
                "a destructor shall not throw");
}
