#include "siliscope/Prototype.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::FunctionDecl;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

void PrototypeCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(unless(isImplicit())).bind("fn"), this);
}

void PrototypeCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !result.Context || result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  if (fn->getBuiltinID() != 0) {
    return;
  }
  if (!fn->hasWrittenPrototype()) {
    reporter.emit(*result.SourceManager,
                  fn->getLocation(),
                  "ss.fn.prototype",
                  "declare this function with a prototype");
    return;
  }
  for (unsigned i = 0; i < fn->getNumParams(); ++i) {
    const auto *p = fn->getParamDecl(i);
    if (!p || p->getIdentifier()) {
      continue;
    }
    reporter.emit(
        *result.SourceManager, p->getLocation(), "ss.fn.prototype", "name this parameter");
  }
}
