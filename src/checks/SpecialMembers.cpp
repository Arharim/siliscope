#include "siliscope/SpecialMembers.h"

#include "siliscope/Report.h"

#include "clang/AST/DeclCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CXXRecordDecl;
using clang::TemplateSpecializationKind;
using clang::TSK_ExplicitInstantiationDeclaration;
using clang::TSK_ExplicitInstantiationDefinition;
using clang::TSK_ImplicitInstantiation;
using clang::ast_matchers::cxxRecordDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

bool instantiation(TemplateSpecializationKind kind) {
  return kind == TSK_ImplicitInstantiation || kind == TSK_ExplicitInstantiationDefinition ||
         kind == TSK_ExplicitInstantiationDeclaration;
}

} // namespace

void SpecialMembersCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(cxxRecordDecl(isDefinition(), unless(isImplicit())).bind("rec"), this);
}

void SpecialMembersCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  const auto *rec = result.Nodes.getNodeAs<CXXRecordDecl>("rec");
  // A default constructor is not one of the five. Rule of zero stays quiet.
  if (!rec || rec->isLambda() || !rec->isCompleteDefinition() ||
      instantiation(rec->getTemplateSpecializationKind())) {
    return;
  }
  const bool dtor = rec->hasUserDeclaredDestructor();
  const bool copyCtor = rec->hasUserDeclaredCopyConstructor();
  const bool copyAssign = rec->hasUserDeclaredCopyAssignment();
  const bool moveCtor = rec->hasUserDeclaredMoveConstructor();
  const bool moveAssign = rec->hasUserDeclaredMoveAssignment();
  const bool any = dtor || copyCtor || copyAssign || moveCtor || moveAssign;
  const bool all = dtor && copyCtor && copyAssign && moveCtor && moveAssign;
  if (!any || all) {
    return;
  }
  reporter.emit(*result.SourceManager,
                rec->getLocation(),
                "ss.cpp.special-members",
                "define or delete all five special members");
}
