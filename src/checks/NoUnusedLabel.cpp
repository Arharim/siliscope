#include "siliscope/NoUnusedLabel.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <set>
#include <string>
#include <vector>

using clang::AddrLabelExpr;
using clang::FunctionDecl;
using clang::GCCAsmStmt;
using clang::GotoStmt;
using clang::LabelDecl;
using clang::LabelStmt;
using clang::Stmt;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

constexpr const char *kId = "ss.fn.no-unused-label";
constexpr const char *kMsg = "this label is unused";

bool allowed(const FunctionDecl *fn, Reporter &reporter) {
  if (!fn || fn->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = fn->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

void gather(const Stmt *stmt,
            std::vector<const LabelStmt *> &defined,
            std::set<const LabelDecl *> &used) {
  if (!stmt) {
    return;
  }
  if (const auto *label = llvm::dyn_cast<LabelStmt>(stmt)) {
    defined.push_back(label);
  } else if (const auto *go = llvm::dyn_cast<GotoStmt>(stmt)) {
    if (const LabelDecl *target = go->getLabel()) {
      used.insert(target);
    }
  } else if (const auto *addr = llvm::dyn_cast<AddrLabelExpr>(stmt)) {
    if (const LabelDecl *target = addr->getLabel()) {
      used.insert(target);
    }
  } else if (const auto *asmStmt = llvm::dyn_cast<GCCAsmStmt>(stmt)) {
    for (unsigned i = 0; i < asmStmt->getNumLabels(); ++i) {
      const AddrLabelExpr *addr = asmStmt->getLabelExpr(i);
      if (addr && addr->getLabel()) {
        used.insert(addr->getLabel());
      }
    }
  }
  for (const Stmt *child : stmt->children()) {
    gather(child, defined, used);
  }
}

} // namespace

void NoUnusedLabelCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
}

void NoUnusedLabelCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !fn->getBody() || !result.SourceManager || allowed(fn, reporter)) {
    return;
  }
  std::vector<const LabelStmt *> defined;
  std::set<const LabelDecl *> used;
  gather(fn->getBody(), defined, used);
  for (const LabelStmt *label : defined) {
    if (used.count(label->getDecl()) != 0) {
      continue;
    }
    reporter.emit(*result.SourceManager, label->getBeginLoc(), kId, kMsg);
  }
}
