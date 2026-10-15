#include "siliscope/MaxParams.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>

using clang::FunctionDecl;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

constexpr const char *kId = "ss.fn.max-params";
constexpr const char *kMsg = "do not pass more than eight parameters";
constexpr unsigned kLimit = 8;

bool hasBody(const FunctionDecl *fn) {
  for (const FunctionDecl *redecl : fn->redecls()) {
    if (redecl->doesThisDeclarationHaveABody()) {
      return true;
    }
  }
  return false;
}

bool allowed(const FunctionDecl *fn, Reporter &reporter) {
  if (!fn || fn->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = fn->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

} // namespace

void MaxParamsCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(unless(isImplicit())).bind("fn"), this);
}

void MaxParamsCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !result.SourceManager || fn->getNumParams() <= kLimit || allowed(fn, reporter)) {
    return;
  }
  if (hasBody(fn)) {
    if (!fn->doesThisDeclarationHaveABody()) {
      return;
    }
  } else if (fn->getCanonicalDecl() != fn) {
    return;
  }
  reporter.emit(*result.SourceManager, fn->getLocation(), kId, kMsg);
}
