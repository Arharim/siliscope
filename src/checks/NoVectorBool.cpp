#include "siliscope/NoVectorBool.h"

#include "siliscope/Report.h"

#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Type.h"
#include "clang/AST/TypeLoc.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CXXTemporaryObjectExpr;
using clang::Decl;
using clang::DeclaratorDecl;
using clang::FunctionDecl;
using clang::QualType;
using clang::TemplateDecl;
using clang::TypedefNameDecl;
using clang::ast_matchers::cxxTemporaryObjectExpr;
using clang::ast_matchers::fieldDecl;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::typedefNameDecl;
using clang::ast_matchers::unless;
using clang::ast_matchers::varDecl;

namespace {

bool inStd(const Decl *d) {
  if (!d) {
    return false;
  }
  const clang::DeclContext *dc = d->getDeclContext();
  while (dc) {
    if (const auto *ns = llvm::dyn_cast<clang::NamespaceDecl>(dc)) {
      if (ns->isStdNamespace()) {
        return true;
      }
    }
    dc = dc->getParent();
  }
  return false;
}

bool containsVectorBool(QualType qt, int depth);

bool vectorBoolArg(const clang::TemplateArgument &arg, int depth) {
  return arg.getKind() == clang::TemplateArgument::Type &&
         containsVectorBool(arg.getAsType(), depth);
}

bool isVectorOfBool(const TemplateDecl *td, const clang::TemplateArgument *args, unsigned count) {
  if (!td || !inStd(td) || td->getName() != "vector" || count < 1) {
    return false;
  }
  if (args[0].getKind() != clang::TemplateArgument::Type) {
    return false;
  }
  QualType argType = args[0].getAsType();
  if (argType.isNull()) {
    return false;
  }
  // typedef bool and const bool are still bool after canonicalization.
  argType = argType.getCanonicalType();
  return argType->isBooleanType();
}

bool containsVectorBool(QualType qt, int depth) {
  if (qt.isNull() || depth > 8) {
    return false;
  }
  qt = qt.getCanonicalType();
  if (qt->isPointerType() || qt->isReferenceType() || qt->isMemberPointerType()) {
    return containsVectorBool(qt->getPointeeType(), depth + 1);
  }
  if (const clang::ArrayType *arr = qt->getAsArrayTypeUnsafe()) {
    return containsVectorBool(arr->getElementType(), depth + 1);
  }
  if (const auto *spec = qt->getAs<clang::TemplateSpecializationType>()) {
    const clang::TemplateArgument *args = spec->template_arguments().data();
    const unsigned count = spec->template_arguments().size();
    if (isVectorOfBool(spec->getTemplateName().getAsTemplateDecl(), args, count)) {
      return true;
    }
    for (const clang::TemplateArgument &arg : spec->template_arguments()) {
      if (vectorBoolArg(arg, depth + 1)) {
        return true;
      }
    }
  }
  const clang::CXXRecordDecl *rec = qt->getAsCXXRecordDecl();
  if (!rec) {
    return false;
  }
  if (const auto *cs = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(rec)) {
    const clang::TemplateArgumentList &list = cs->getTemplateArgs();
    if (isVectorOfBool(cs->getSpecializedTemplate(), list.data(), list.size())) {
      return true;
    }
    for (const clang::TemplateArgument &arg : list.asArray()) {
      if (vectorBoolArg(arg, depth + 1)) {
        return true;
      }
    }
  }
  const clang::CXXRecordDecl *def = rec->getDefinition();
  if (!def) {
    return false;
  }
  for (const clang::CXXBaseSpecifier &base : def->bases()) {
    if (containsVectorBool(base.getType(), depth + 1)) {
      return true;
    }
  }
  return false;
}

clang::SourceLocation typeBegin(const Decl *d) {
  const clang::TypeSourceInfo *tsi = nullptr;
  if (const auto *dd = llvm::dyn_cast<DeclaratorDecl>(d)) {
    tsi = dd->getTypeSourceInfo();
  } else if (const auto *td = llvm::dyn_cast<TypedefNameDecl>(d)) {
    tsi = td->getTypeSourceInfo();
  }
  if (tsi) {
    const clang::SourceLocation loc = tsi->getTypeLoc().getBeginLoc();
    if (loc.isValid()) {
      return loc;
    }
  }
  return d->getLocation();
}

QualType declaredType(const Decl *d) {
  if (const auto *fd = llvm::dyn_cast<FunctionDecl>(d)) {
    return fd->getReturnType();
  }
  if (const auto *td = llvm::dyn_cast<TypedefNameDecl>(d)) {
    return td->getUnderlyingType();
  }
  if (const auto *vd = llvm::dyn_cast<clang::ValueDecl>(d)) {
    return vd->getType();
  }
  return {};
}

} // namespace

void NoVectorBoolCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(varDecl(unless(isImplicit())).bind("decl"), this);
  finder.addMatcher(fieldDecl(unless(isImplicit())).bind("decl"), this);
  finder.addMatcher(functionDecl(unless(isImplicit())).bind("decl"), this);
  finder.addMatcher(typedefNameDecl().bind("decl"), this);
  finder.addMatcher(cxxTemporaryObjectExpr().bind("tmp"), this);
}

void NoVectorBoolCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  if (const auto *d = result.Nodes.getNodeAs<Decl>("decl")) {
    if (!containsVectorBool(declaredType(d), 0)) {
      return;
    }
    reporter.emit(
        *result.SourceManager, typeBegin(d), "ss.cpp.no-vector-bool", "do not use vector of bool");
    return;
  }
  const auto *tmp = result.Nodes.getNodeAs<CXXTemporaryObjectExpr>("tmp");
  if (!tmp || !containsVectorBool(tmp->getType(), 0)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                tmp->getBeginLoc(),
                "ss.cpp.no-vector-bool",
                "do not use vector of bool");
}
