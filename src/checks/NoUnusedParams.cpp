#include "siliscope/NoUnusedParams.h"

#include "siliscope/Report.h"

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::FunctionDecl;
using clang::ParmVarDecl;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

void NoUnusedParamsCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
}

void NoUnusedParamsCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !fn->hasBody() || fn->getBuiltinID() != 0) {
    return;
  }
  for (unsigned i = 0; i < fn->getNumParams(); ++i) {
    const ParmVarDecl *p = fn->getParamDecl(i);
    if (!p || !p->getIdentifier()) {
      continue;
    }
    if (p->hasAttr<clang::UnusedAttr>() || p->hasAttr<clang::UsedAttr>()) {
      continue;
    }
    if (p->isReferenced() || p->isUsed()) {
      continue;
    }
    reporter.emit(*result.SourceManager,
                  p->getLocation(),
                  "ss.fn.no-unused-params",
                  "use this parameter, omit it, or mark it unused");
  }
}
