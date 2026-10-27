#include "siliscope/NoRecursiveMutex.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <string>

using clang::CallExpr;
using clang::DeclRefExpr;
using clang::FieldDecl;
using clang::FunctionDecl;
using clang::ParmVarDecl;
using clang::QualType;
using clang::TagDecl;
using clang::TypeDecl;
using clang::VarDecl;
using clang::ast_matchers::callExpr;
using clang::ast_matchers::declRefExpr;
using clang::ast_matchers::fieldDecl;
using clang::ast_matchers::parmVarDecl;
using clang::ast_matchers::tagDecl;
using clang::ast_matchers::varDecl;

namespace {

constexpr const char *kId = "ss.conc.no-recursive-mutex";
constexpr const char *kMsg = "do not use a recursive mutex";

bool recursiveMutex(llvm::StringRef name) {
  return name.contains_insensitive("recursive") && name.contains_insensitive("mutex");
}

bool typeIsRecursive(QualType type) {
  if (type.isNull() || type->isDependentType()) {
    return false;
  }
  if (recursiveMutex(type.getAsString())) {
    return true;
  }
  const clang::TagDecl *tag = type->getAsTagDecl();
  return tag && tag->getIdentifier() && recursiveMutex(tag->getName());
}

bool functionAllowed(const FunctionDecl *fn, Reporter &reporter) {
  if (!fn || fn->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = fn->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

} // namespace

const FunctionDecl *enclosing(const clang::Stmt *stmt, clang::ASTContext &ctx) {
  clang::DynTypedNode node = clang::DynTypedNode::create(*stmt);
  for (int step = 0; step < 64; ++step) {
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      return nullptr;
    }
    node = parents[0];
    if (const auto *fn = node.get<FunctionDecl>()) {
      return fn;
    }
  }
  return nullptr;
}

void NoRecursiveMutexCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(varDecl().bind("var"), this);
  finder.addMatcher(fieldDecl().bind("field"), this);
  finder.addMatcher(tagDecl().bind("tag"), this);
  finder.addMatcher(callExpr().bind("call"), this);
  finder.addMatcher(declRefExpr().bind("ref"), this);
}

void NoRecursiveMutexCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.SourceManager || !result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  const clang::SourceManager &sm = *result.SourceManager;
  if (const auto *var = result.Nodes.getNodeAs<VarDecl>("var")) {
    if (var->isImplicit() || !typeIsRecursive(var->getType())) {
      return;
    }
    if (functionAllowed(llvm::dyn_cast<FunctionDecl>(var->getDeclContext()), reporter)) {
      return;
    }
    reporter.emit(sm, var->getLocation(), kId, kMsg);
    return;
  }
  if (const auto *field = result.Nodes.getNodeAs<FieldDecl>("field")) {
    if (!typeIsRecursive(field->getType())) {
      return;
    }
    reporter.emit(sm, field->getLocation(), kId, kMsg);
    return;
  }
  if (const auto *tag = result.Nodes.getNodeAs<TagDecl>("tag")) {
    if (!tag->getIdentifier() || !recursiveMutex(tag->getName()) ||
        !tag->isThisDeclarationADefinition()) {
      return;
    }
    reporter.emit(sm, tag->getLocation(), kId, kMsg);
    return;
  }
  if (const auto *call = result.Nodes.getNodeAs<CallExpr>("call")) {
    const FunctionDecl *callee = call->getDirectCallee();
    if (!callee || !callee->getIdentifier() || !recursiveMutex(callee->getName())) {
      return;
    }
    if (functionAllowed(enclosing(call, *result.Context), reporter)) {
      return;
    }
    reporter.emit(sm, call->getBeginLoc(), kId, kMsg);
    return;
  }
  if (const auto *ref = result.Nodes.getNodeAs<DeclRefExpr>("ref")) {
    const clang::NamedDecl *named = ref->getDecl();
    if (!named || !named->getIdentifier() || !recursiveMutex(named->getName())) {
      return;
    }
    if (llvm::isa<FunctionDecl>(named) || llvm::isa<TagDecl>(named) || llvm::isa<TypeDecl>(named)) {
      return;
    }
    reporter.emit(sm, ref->getBeginLoc(), kId, kMsg);
  }
}
