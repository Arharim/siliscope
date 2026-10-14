#include "siliscope/NoRestrict.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>

using clang::ExplicitCastExpr;
using clang::QualType;
using clang::ValueDecl;
using clang::ast_matchers::explicitCastExpr;
using clang::ast_matchers::valueDecl;

namespace {

constexpr const char *kId = "ss.type.no-restrict";
constexpr const char *kMsg = "do not use restrict";

bool hasRestrict(QualType type) {
  while (!type.isNull()) {
    if (type.isRestrictQualified()) {
      return true;
    }
    const clang::Type *inner = type.getTypePtr();
    if (const auto *pointer = llvm::dyn_cast<clang::PointerType>(inner)) {
      type = pointer->getPointeeType();
      continue;
    }
    if (const auto *array = llvm::dyn_cast<clang::ArrayType>(inner)) {
      type = array->getElementType();
      continue;
    }
    if (const auto *paren = llvm::dyn_cast<clang::ParenType>(inner)) {
      type = paren->getInnerType();
      continue;
    }
    return false;
  }
  return false;
}

bool allowed(const clang::NamedDecl *decl, Reporter &reporter) {
  if (!decl || decl->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = decl->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

} // namespace

void NoRestrictCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(valueDecl().bind("decl"), this);
  finder.addMatcher(explicitCastExpr().bind("cast"), this);
}

void NoRestrictCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.SourceManager || result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  if (const auto *decl = result.Nodes.getNodeAs<ValueDecl>("decl")) {
    if (decl->isImplicit() || !hasRestrict(decl->getType()) || allowed(decl, reporter)) {
      return;
    }
    reporter.emit(*result.SourceManager, decl->getLocation(), kId, kMsg);
    return;
  }
  const auto *cast = result.Nodes.getNodeAs<ExplicitCastExpr>("cast");
  if (!cast || !hasRestrict(cast->getType())) {
    return;
  }
  reporter.emit(*result.SourceManager, cast->getBeginLoc(), kId, kMsg);
}
