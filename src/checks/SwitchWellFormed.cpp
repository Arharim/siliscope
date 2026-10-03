#include "siliscope/SwitchWellFormed.h"

#include "siliscope/Report.h"
#include "siliscope/SwitchClause.h"

#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "llvm/ADT/SmallVector.h"

using clang::CompoundStmt;
using clang::DefaultStmt;
using clang::Expr;
using clang::QualType;
using clang::SwitchCase;
using clang::SwitchStmt;
using clang::ast_matchers::switchStmt;
using siliscope::switch_clause::Clause;
using siliscope::switch_clause::clauseEmpty;
using siliscope::switch_clause::clauseTerminates;
using siliscope::switch_clause::collectClauses;
using siliscope::switch_clause::topLevelCases;

namespace {

constexpr const char *kId = "ss.ctrl.switch-well-formed";

} // namespace

void SwitchWellFormedCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(switchStmt().bind("sw"), this);
}

void SwitchWellFormedCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *sw = result.Nodes.getNodeAs<SwitchStmt>("sw");
  if (!sw || !result.SourceManager) {
    return;
  }
  const clang::SourceManager &sm = *result.SourceManager;
  const auto *body = llvm::dyn_cast_or_null<CompoundStmt>(sw->getBody());
  if (!body) {
    reporter.emit(sm, sw->getSwitchLoc(), kId, "switch body must be a compound statement");
    return;
  }

  if (const Expr *cond = sw->getCond()) {
    const QualType written = cond->IgnoreParenImpCasts()->getType();
    // C `x == 1` has type int. bool and _Bool do not.
    if (!written.isNull() && written->isBooleanType()) {
      reporter.emit(sm, sw->getSwitchLoc(), kId, "switch condition is boolean");
    }
  }

  const auto allowed = topLevelCases(body);
  bool hasDefault = false;
  for (const SwitchCase *label = sw->getSwitchCaseList(); label;
       label = label->getNextSwitchCase()) {
    if (!allowed.contains(label)) {
      reporter.emit(
          sm, label->getBeginLoc(), kId, "case label is not at the top level of the switch");
    }
    if (llvm::isa<DefaultStmt>(label)) {
      hasDefault = true;
    }
  }
  if (!hasDefault) {
    reporter.emit(sm, sw->getSwitchLoc(), kId, "switch has no default");
  }

  llvm::SmallVector<Clause, 8> clauses;
  collectClauses(body, clauses);
  for (const Clause &clause : clauses) {
    if (!clause.label) {
      continue;
    }
    if (clauseEmpty(clause)) {
      reporter.emit(sm, clause.label->getBeginLoc(), kId, "switch clause is empty");
      continue;
    }
    // The next label is the fall-through rule. This rule only catches a clause
    // that runs off the end of the switch.
    if (!clause.next && !clauseTerminates(clause)) {
      reporter.emit(
          sm, clause.stmts.back()->getBeginLoc(), kId, "clause does not end with break or throw");
    }
  }
}
