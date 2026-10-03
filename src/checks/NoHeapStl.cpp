#include "siliscope/NoHeapStl.h"

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

bool heapStlName(llvm::StringRef name) {
  return name == "vector" || name == "deque" || name == "list" || name == "forward_list" ||
         name == "map" || name == "multimap" || name == "set" || name == "multiset" ||
         name == "unordered_map" || name == "unordered_multimap" || name == "unordered_set" ||
         name == "unordered_multiset" || name == "basic_string" || name == "function" ||
         name == "basic_istream" || name == "basic_ostream" || name == "basic_iostream" ||
         name == "basic_ifstream" || name == "basic_ofstream" || name == "basic_fstream" ||
         name == "basic_istringstream" || name == "basic_ostringstream" ||
         name == "basic_stringstream" || name == "stack" || name == "queue" ||
         name == "priority_queue";
}

// std::pmr is an ordinary namespace inside std, not an inline one.
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

bool namedHeap(const TemplateDecl *td) {
  return td && inStd(td) && heapStlName(td->getName());
}

bool containsHeapStl(QualType qt, int depth);

bool heapArg(const clang::TemplateArgument &arg, int depth) {
  return arg.getKind() == clang::TemplateArgument::Type && containsHeapStl(arg.getAsType(), depth);
}

bool containsHeapStl(QualType qt, int depth) {
  if (qt.isNull() || depth > 8) {
    return false;
  }
  qt = qt.getCanonicalType();
  if (qt->isPointerType() || qt->isReferenceType() || qt->isMemberPointerType()) {
    return containsHeapStl(qt->getPointeeType(), depth + 1);
  }
  if (const clang::ArrayType *arr = qt->getAsArrayTypeUnsafe()) {
    return containsHeapStl(arr->getElementType(), depth + 1);
  }
  if (const auto *spec = qt->getAs<clang::TemplateSpecializationType>()) {
    if (namedHeap(spec->getTemplateName().getAsTemplateDecl())) {
      return true;
    }
    for (const clang::TemplateArgument &arg : spec->template_arguments()) {
      if (heapArg(arg, depth + 1)) {
        return true;
      }
    }
  }
  const clang::CXXRecordDecl *rec = qt->getAsCXXRecordDecl();
  if (!rec) {
    return false;
  }
  if (const auto *cs = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(rec)) {
    if (namedHeap(cs->getSpecializedTemplate())) {
      return true;
    }
    for (const clang::TemplateArgument &arg : cs->getTemplateArgs().asArray()) {
      if (heapArg(arg, depth + 1)) {
        return true;
      }
    }
  }
  // bases() asserts on a declaration that was never defined.
  const clang::CXXRecordDecl *def = rec->getDefinition();
  if (!def) {
    return false;
  }
  for (const clang::CXXBaseSpecifier &base : def->bases()) {
    if (containsHeapStl(base.getType(), depth + 1)) {
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

void NoHeapStlCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(varDecl(unless(isImplicit())).bind("decl"), this);
  finder.addMatcher(fieldDecl(unless(isImplicit())).bind("decl"), this);
  finder.addMatcher(functionDecl(unless(isImplicit())).bind("decl"), this);
  finder.addMatcher(typedefNameDecl().bind("decl"), this);
  finder.addMatcher(cxxTemporaryObjectExpr().bind("tmp"), this);
}

void NoHeapStlCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  if (const auto *d = result.Nodes.getNodeAs<Decl>("decl")) {
    if (!containsHeapStl(declaredType(d), 0)) {
      return;
    }
    reporter.emit(*result.SourceManager,
                  typeBegin(d),
                  "ss.cpp.no-heap-stl",
                  "do not use a heap-backed standard container");
    return;
  }
  const auto *tmp = result.Nodes.getNodeAs<CXXTemporaryObjectExpr>("tmp");
  if (!tmp || !containsHeapStl(tmp->getType(), 0)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                tmp->getBeginLoc(),
                "ss.cpp.no-heap-stl",
                "do not use a heap-backed standard container");
}
