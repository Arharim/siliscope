#include "siliscope/ArraySize.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>

using clang::FieldDecl;
using clang::ParmVarDecl;
using clang::VarDecl;
using clang::ast_matchers::fieldDecl;
using clang::ast_matchers::varDecl;

namespace {

constexpr const char *kId = "ss.type.array-size";
constexpr const char *kMsg = "specify the array size";

bool allowed(const clang::NamedDecl *decl, Reporter &reporter) {
  if (!decl || decl->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = decl->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

bool unspecified(const clang::DeclaratorDecl *decl) {
  if (!decl->getTypeSourceInfo()) {
    return decl->getType()->isIncompleteArrayType();
  }
  return decl->getTypeSourceInfo()->getType()->isIncompleteArrayType();
}

} // namespace

void ArraySizeCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(varDecl().bind("var"), this);
  finder.addMatcher(fieldDecl().bind("field"), this);
}

void ArraySizeCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.SourceManager) {
    return;
  }
  if (const auto *var = result.Nodes.getNodeAs<VarDecl>("var")) {
    // A parameter T a[] decays to a pointer. An initializer fixes the bound.
    if (llvm::isa<ParmVarDecl>(var) || var->hasInit() || var->isImplicit() || !unspecified(var) ||
        allowed(var, reporter)) {
      return;
    }
    reporter.emit(*result.SourceManager, var->getLocation(), kId, kMsg);
    return;
  }
  const auto *field = result.Nodes.getNodeAs<FieldDecl>("field");
  if (!field || field->isImplicit() || !unspecified(field) || allowed(field, reporter)) {
    return;
  }
  reporter.emit(*result.SourceManager, field->getLocation(), kId, kMsg);
}
