#include "siliscope/NoBlockScope.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::FunctionDecl;
using clang::ast_matchers::cxxMethodDecl;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::hasAncestor;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

void NoBlockScopeCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(
      functionDecl(hasAncestor(functionDecl()), unless(isImplicit()), unless(cxxMethodDecl()))
          .bind("fn"),
      this);
}

void NoBlockScopeCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn) {
    return;
  }
  reporter.emit(*result.SourceManager,
                fn->getLocation(),
                "ss.fn.no-block-scope",
                "do not declare functions at block scope");
}
