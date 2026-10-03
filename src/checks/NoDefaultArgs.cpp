#include "siliscope/NoDefaultArgs.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::Expr;
using clang::FunctionDecl;
using clang::ParmVarDecl;
using clang::SourceLocation;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

void NoDefaultArgsCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(unless(isImplicit())).bind("fn"), this);
}

void NoDefaultArgsCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || fn->isTemplateInstantiation()) {
    return;
  }
  for (const ParmVarDecl *param : fn->parameters()) {
    if (!param || !param->hasDefaultArg() || param->hasInheritedDefaultArg()) {
      continue;
    }
    SourceLocation loc = param->getLocation();
    if (!param->hasUnparsedDefaultArg() && !param->hasUninstantiatedDefaultArg()) {
      if (const Expr *arg = param->getDefaultArg()) {
        if (arg->getExprLoc().isValid()) {
          loc = arg->getExprLoc();
        }
      }
    }
    reporter.emit(*result.SourceManager,
                  loc,
                  "ss.cpp.no-default-args",
                  "do not give a parameter a default argument");
  }
}
