#include "siliscope/PrivateData.h"

#include "siliscope/RecordKind.h"
#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/Specifiers.h"

using clang::AccessSpecifier;
using clang::CXXRecordDecl;
using clang::Decl;
using clang::FieldDecl;
using clang::VarDecl;
using clang::ast_matchers::fieldDecl;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;
using clang::ast_matchers::varDecl;

namespace {

const CXXRecordDecl *memberRecord(const Decl *decl) {
  return llvm::dyn_cast<CXXRecordDecl>(decl->getDeclContext());
}

} // namespace

void PrivateDataCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(fieldDecl(unless(isImplicit())).bind("field"), this);
  finder.addMatcher(varDecl(unless(isImplicit())).bind("static"), this);
}

void PrivateDataCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  const Decl *decl = nullptr;
  if (const auto *field = result.Nodes.getNodeAs<FieldDecl>("field")) {
    if (field->isUnnamedBitField()) {
      return;
    }
    decl = field;
  } else if (const auto *var = result.Nodes.getNodeAs<VarDecl>("static")) {
    if (!var->isStaticDataMember()) {
      return;
    }
    decl = var;
  } else {
    return;
  }
  if (decl->getAccess() == AccessSpecifier::AS_private) {
    return;
  }
  const CXXRecordDecl *rec = memberRecord(decl);
  if (!rec || allowsPublicSurface(rec, *result.Context)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                decl->getLocation(),
                "ss.cpp.private-data",
                "make this data member private");
}
