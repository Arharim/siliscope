#include "siliscope/VirtualDtor.h"

#include "siliscope/Report.h"

#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/Specifiers.h"

using clang::AccessSpecifier;
using clang::CXXBaseSpecifier;
using clang::CXXDestructorDecl;
using clang::CXXRecordDecl;
using clang::ast_matchers::cxxRecordDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

// A virtual or deleted destructor anywhere in the base chain makes the
// implicit destructor of this class virtual or deleted. A protected
// non-virtual base destructor does not: the derived destructor stays public.
bool baseCoversImplicitDtor(const CXXRecordDecl *rec, int depth) {
  if (!rec || depth > 16) {
    return false;
  }
  const CXXRecordDecl *def = rec->getDefinition();
  if (!def) {
    return false;
  }
  if (const CXXDestructorDecl *dtor = def->getDestructor()) {
    return dtor->isVirtual() || dtor->isDeleted();
  }
  for (const CXXBaseSpecifier &base : def->bases()) {
    const auto *baseRec = base.getType()->getAsCXXRecordDecl();
    if (baseRec && baseCoversImplicitDtor(baseRec, depth + 1)) {
      return true;
    }
  }
  return false;
}

bool instantiation(const CXXRecordDecl *rec) {
  switch (rec->getTemplateSpecializationKind()) {
  case clang::TSK_ImplicitInstantiation:
  case clang::TSK_ExplicitInstantiationDeclaration:
  case clang::TSK_ExplicitInstantiationDefinition:
    return true;
  default:
    return false;
  }
}

} // namespace

void VirtualDtorCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(cxxRecordDecl(isDefinition(), unless(isImplicit())).bind("rec"), this);
}

void VirtualDtorCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  const auto *rec = result.Nodes.getNodeAs<CXXRecordDecl>("rec");
  if (!rec || !rec->isCompleteDefinition() || rec->isLambda() || rec->isUnion() ||
      rec->isInvalidDecl()) {
    return;
  }
  // A final class cannot be a base, so delete-through-base does not apply.
  if (rec->hasAttr<clang::FinalAttr>() || !rec->isPolymorphic() || instantiation(rec)) {
    return;
  }
  const CXXDestructorDecl *dtor = rec->getDestructor();
  if (dtor && (dtor->isVirtual() || dtor->isDeleted() ||
               dtor->getAccess() == AccessSpecifier::AS_protected)) {
    return;
  }
  // The implicit destructor may not be in the AST yet. A virtual destructor
  // above this class still makes that implicit one virtual.
  if (!dtor && baseCoversImplicitDtor(rec, 0)) {
    return;
  }
  clang::SourceLocation loc = rec->getLocation();
  if (dtor && !dtor->isImplicit() && dtor->getLocation().isValid()) {
    loc = dtor->getLocation();
  }
  reporter.emit(*result.SourceManager,
                loc,
                "ss.cpp.virtual-dtor",
                "polymorphic base needs a virtual, protected, or deleted destructor");
}
