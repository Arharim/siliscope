#include "siliscope/ExplicitType.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>

using clang::DeclaratorDecl;
using clang::ast_matchers::declaratorDecl;

namespace {

constexpr const char *kId = "ss.type.explicit";
constexpr const char *kMsg = "specify the type explicitly";

bool allowed(const DeclaratorDecl *decl, Reporter &reporter) {
  if (!decl || decl->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = decl->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

} // namespace

void ExplicitTypeCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(declaratorDecl().bind("decl"), this);
}

void ExplicitTypeCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *decl = result.Nodes.getNodeAs<DeclaratorDecl>("decl");
  if (!decl || !result.Context || !result.SourceManager ||
      result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  if (decl->isImplicit() || allowed(decl, reporter)) {
    return;
  }
  const clang::SourceLocation spec = decl->getTypeSpecStartLoc();
  const clang::SourceLocation name = decl->getLocation();
  // GNU C keeps implicit int. The type-spec location is then missing, or it does not
  // sit strictly before the declarator name (a function such as `static value(void)`).
  if (spec.isValid() &&
      (!name.isValid() || result.SourceManager->isBeforeInTranslationUnit(spec, name))) {
    return;
  }
  reporter.emit(*result.SourceManager, decl->getLocation(), kId, kMsg);
}
