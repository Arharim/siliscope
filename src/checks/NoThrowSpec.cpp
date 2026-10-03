#include "siliscope/NoThrowSpec.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/ExceptionSpecificationType.h"

using clang::ExceptionSpecificationType;
using clang::FunctionDecl;
using clang::FunctionProtoType;
using clang::SourceLocation;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

bool dynamicSpec(ExceptionSpecificationType est) {
  return est == clang::EST_Dynamic || est == clang::EST_DynamicNone || est == clang::EST_MSAny;
}

} // namespace

void NoThrowSpecCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(unless(isImplicit())).bind("fn"), this);
}

void NoThrowSpecCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || fn != fn->getCanonicalDecl() || fn->isTemplateInstantiation()) {
    return;
  }
  const auto *proto = fn->getType()->getAs<FunctionProtoType>();
  if (!proto || !dynamicSpec(proto->getExceptionSpecType())) {
    return;
  }
  SourceLocation loc = fn->getExceptionSpecSourceRange().getBegin();
  if (loc.isInvalid()) {
    loc = fn->getLocation();
  }
  reporter.emit(*result.SourceManager,
                loc,
                "ss.cpp.no-throw-spec",
                "do not use a dynamic exception specification");
}
