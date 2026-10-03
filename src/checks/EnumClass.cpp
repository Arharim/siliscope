#include "siliscope/EnumClass.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::EnumDecl;
using clang::SourceLocation;
using clang::ast_matchers::enumDecl;

void EnumClassCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(enumDecl().bind("enum"), this);
}

void EnumClassCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  // enum class is C++. A C translation unit under strict keeps a plain enum.
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  const auto *decl = result.Nodes.getNodeAs<EnumDecl>("enum");
  if (!decl || decl->isScoped()) {
    return;
  }
  if (const EnumDecl *def = decl->getDefinition()) {
    if (decl != def) {
      return;
    }
  }
  SourceLocation loc = decl->getLocation();
  if (!decl->getIdentifier() || loc.isInvalid()) {
    loc = decl->getBeginLoc();
  }
  reporter.emit(*result.SourceManager, loc, "ss.cpp.enum-class", "use enum class");
}
