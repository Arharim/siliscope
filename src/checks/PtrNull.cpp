#include "siliscope/PtrNull.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::QualType;
using clang::VarDecl;
using clang::ast_matchers::hasAutomaticStorageDuration;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::parmVarDecl;
using clang::ast_matchers::unless;
using clang::ast_matchers::varDecl;

namespace {

static bool isPtrObject(QualType t) {
  if (t.isNull()) {
    return false;
  }
  t = t.getCanonicalType();
  if (const clang::ArrayType *a = t->getAsArrayTypeUnsafe()) {
    return isPtrObject(a->getElementType());
  }
  return t->isAnyPointerType();
}

} // namespace

void PtrNullCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(
      varDecl(hasAutomaticStorageDuration(), unless(parmVarDecl()), unless(isImplicit()))
          .bind("var"),
      this);
}

void PtrNullCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *d = result.Nodes.getNodeAs<VarDecl>("var");
  if (!d || d->hasInit() || !isPtrObject(d->getType())) {
    return;
  }
  reporter.emit(*result.SourceManager,
                d->getLocation(),
                "ss.decl.ptr-null",
                "initialize a pointer to NULL / nullptr if it has no address");
}
