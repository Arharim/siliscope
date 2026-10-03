#include "siliscope/NoUsingInHeader.h"

#include "siliscope/Report.h"

#include "clang/AST/DeclCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

using clang::CXXRecordDecl;
using clang::Decl;
using clang::SourceManager;
using clang::UsingDecl;
using clang::UsingDirectiveDecl;
using clang::ast_matchers::usingDecl;
using clang::ast_matchers::usingDirectiveDecl;

namespace {

bool writtenInMainFile(const SourceManager &sm, clang::SourceLocation loc) {
  if (loc.isInvalid()) {
    return false;
  }
  return sm.isWrittenInMainFile(sm.getSpellingLoc(loc));
}

} // namespace

void NoUsingInHeaderCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(usingDirectiveDecl().bind("dir"), this);
  finder.addMatcher(usingDecl().bind("use"), this);
}

void NoUsingInHeaderCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  const Decl *decl = nullptr;
  if (const auto *dir = result.Nodes.getNodeAs<UsingDirectiveDecl>("dir")) {
    decl = dir;
  } else if (const auto *use = result.Nodes.getNodeAs<UsingDecl>("use")) {
    // using Base::name inside the class is the interface, not a namespace import.
    if (llvm::isa<CXXRecordDecl>(use->getDeclContext())) {
      return;
    }
    decl = use;
  } else {
    return;
  }
  if (writtenInMainFile(*result.SourceManager, decl->getLocation())) {
    return;
  }
  reporter.emit(*result.SourceManager,
                decl->getLocation(),
                "ss.cpp.no-using-in-header",
                "a header does not pull a name in with using");
}
