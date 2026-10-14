#include "siliscope/IsrMarked.h"

#include "siliscope/Report.h"

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>

using clang::DeclRefExpr;
using clang::Expr;
using clang::FunctionDecl;
using clang::InitListExpr;
using clang::SectionAttr;
using clang::VarDecl;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;
using clang::ast_matchers::varDecl;

namespace {

constexpr const char *kId = "ss.emb.isr-marked";
constexpr const char *kMsg = "declare this handler with the interrupt ABI";

bool allowed(const FunctionDecl *fn, Reporter &reporter) {
  if (!fn || !fn->getIdentifier()) {
    return false;
  }
  const std::string name = fn->getName().str();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

bool interruptAbi(const FunctionDecl *fn) {
  for (const FunctionDecl *redecl : fn->redecls()) {
    if (!redecl->hasAttrs()) {
      continue;
    }
    for (const clang::Attr *attr : redecl->attrs()) {
      if (!attr) {
        continue;
      }
      const llvm::StringRef spelling = attr->getSpelling();
      if (spelling.contains_insensitive("interrupt")) {
        return true;
      }
    }
  }
  return false;
}

// CMSIS startup spells the vector entry as the name. That is the marking.
bool cmsisHandler(const FunctionDecl *fn) {
  if (!fn->getIdentifier() || fn->getNumParams() != 0 || !fn->getReturnType()->isVoidType()) {
    return false;
  }
  const llvm::StringRef name = fn->getName();
  return name.ends_with("_IRQHandler") || name.ends_with("_Handler");
}

bool marked(const FunctionDecl *fn) {
  return interruptAbi(fn) || cmsisHandler(fn);
}

bool houseIsr(const FunctionDecl *fn) {
  return fn->getIdentifier() && fn->getName().ends_with("_isr") && fn->getNumParams() == 0 &&
         fn->getReturnType()->isVoidType();
}

bool vectorTable(const VarDecl *var) {
  if (var->getIdentifier() && var->getName().contains_insensitive("vector")) {
    return true;
  }
  if (const auto *section = var->getAttr<SectionAttr>()) {
    return section->getName().contains_insensitive("vector");
  }
  return false;
}

class FindHandlers : public clang::RecursiveASTVisitor<FindHandlers> {
public:
  llvm::SmallVector<const FunctionDecl *, 8> handlers;

  bool VisitDeclRefExpr(DeclRefExpr *expr) {
    if (const auto *fn = llvm::dyn_cast<FunctionDecl>(expr->getDecl())) {
      handlers.push_back(fn->getCanonicalDecl());
    }
    return true;
  }
};

} // namespace

void IsrMarkedCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
  finder.addMatcher(varDecl(isDefinition(), unless(isImplicit())).bind("var"), this);
}

void IsrMarkedCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.SourceManager) {
    return;
  }
  if (const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn")) {
    if (fn->isThisDeclarationADefinition() && houseIsr(fn) && !marked(fn) &&
        !allowed(fn, reporter)) {
      reporter.emit(*result.SourceManager, fn->getLocation(), kId, kMsg);
    }
    return;
  }
  const auto *var = result.Nodes.getNodeAs<VarDecl>("var");
  if (!var || !var->getInit() || !vectorTable(var)) {
    return;
  }
  FindHandlers walk;
  walk.TraverseStmt(const_cast<Expr *>(var->getInit()));
  for (const FunctionDecl *fn : walk.handlers) {
    if (marked(fn) || allowed(fn, reporter)) {
      continue;
    }
    const FunctionDecl *at = fn->getDefinition();
    if (!at) {
      at = fn;
    }
    reporter.emit(*result.SourceManager, at->getLocation(), kId, kMsg);
  }
}
