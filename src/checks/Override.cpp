#include "siliscope/Override.h"

#include "siliscope/Report.h"

#include "clang/AST/Attr.h"
#include "clang/AST/DeclCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::CXXMethodDecl;
using clang::ast_matchers::cxxMethodDecl;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::isOverride;
using clang::ast_matchers::unless;

void OverrideCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(cxxMethodDecl(isOverride(), unless(isImplicit())).bind("method"), this);
}

void OverrideCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  // strict enables this rule for a C translation unit too.
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  const auto *method = result.Nodes.getNodeAs<CXXMethodDecl>("method");
  if (!method || method->size_overridden_methods() == 0) {
    return;
  }
  // The specifier lives on the first declaration. An out-of-line body cannot
  // repeat it. final closes the override, so it counts as the mark.
  const auto *canon = method->getCanonicalDecl();
  if (canon->hasAttr<clang::OverrideAttr>() || canon->hasAttr<clang::FinalAttr>()) {
    return;
  }
  if (method != canon) {
    return;
  }
  reporter.emit(*result.SourceManager,
                method->getLocation(),
                "ss.cpp.override",
                "mark an overriding function with override");
}
