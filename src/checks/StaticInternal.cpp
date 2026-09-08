#include "siliscope/StaticInternal.h"

#include "siliscope/Isr.h"
#include "siliscope/Report.h"

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/Linkage.h"
#include "clang/Basic/SourceManager.h"

using clang::FunctionDecl;
using clang::NamedDecl;
using clang::VarDecl;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;
using clang::ast_matchers::varDecl;

namespace {

static bool exportedViaHeader(const NamedDecl *d, const clang::SourceManager &sm) {
  for (const auto *r : d->redecls()) {
    clang::SourceLocation loc = r->getLocation();
    if (loc.isInvalid()) {
      continue;
    }
    loc = sm.getExpansionLoc(loc);
    if (!sm.isWrittenInMainFile(loc)) {
      return true;
    }
  }
  return false;
}

static bool isExternalFileScope(const NamedDecl *d) {
  return d && d->getFormalLinkage() == clang::Linkage::External;
}

static bool skipFn(const FunctionDecl *fn) {
  if (!fn || fn->isImplicit() || fn->getBuiltinID() != 0 || fn->isMain()) {
    return true;
  }
  if (fn->hasAttr<clang::WeakAttr>() || fn->hasAttr<clang::AliasAttr>()) {
    return true;
  }
  return isIsr(fn);
}

static bool skipVar(const VarDecl *vd) {
  if (!vd || vd->isImplicit() || !vd->isFileVarDecl()) {
    return true;
  }
  if (vd->hasAttr<clang::WeakAttr>() || vd->hasAttr<clang::AliasAttr>()) {
    return true;
  }
  return false;
}

} // namespace

void StaticInternalCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
  finder.addMatcher(varDecl(unless(isImplicit())).bind("var"), this);
}

void StaticInternalCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.SourceManager || !result.Context || result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  const clang::SourceManager &sm = *result.SourceManager;
  const NamedDecl *d = nullptr;
  if (const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn")) {
    if (skipFn(fn) || !fn->isThisDeclarationADefinition()) {
      return;
    }
    d = fn;
  } else if (const auto *vd = result.Nodes.getNodeAs<VarDecl>("var")) {
    if (skipVar(vd) || !vd->isThisDeclarationADefinition()) {
      return;
    }
    d = vd;
  }
  if (!d || !isExternalFileScope(d) || exportedViaHeader(d, sm)) {
    return;
  }
  reporter.emit(*result.SourceManager,
                d->getLocation(),
                "ss.fn.static-internal",
                "give this file-local symbol static linkage");
}
