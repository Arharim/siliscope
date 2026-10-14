#include "siliscope/InlineIsStatic.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/Linkage.h"
#include "clang/Basic/SourceManager.h"

#include <string>

using clang::FunctionDecl;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

constexpr const char *kId = "ss.fn.inline-is-static";

bool allowed(const FunctionDecl *fn, Reporter &reporter) {
  if (fn->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = fn->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

} // namespace

void InlineIsStaticCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
}

void InlineIsStaticCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !result.Context || !result.SourceManager || result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  if (!fn->isInlineSpecified() || fn->getFormalLinkage() == clang::Linkage::Internal ||
      !fn->doesThisDeclarationHaveABody()) {
    return;
  }
  const clang::SourceLocation loc = fn->getLocation();
  if (loc.isInvalid() || !result.SourceManager->isWrittenInMainFile(loc)) {
    return;
  }
  if (allowed(fn, reporter)) {
    return;
  }
  reporter.emit(*result.SourceManager, loc, kId, "make this inline function static");
}
