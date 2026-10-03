#include "siliscope/SwitchFallthroughComment.h"

#include "siliscope/Report.h"
#include "siliscope/SwitchClause.h"

#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "llvm/ADT/SmallVector.h"

using clang::CompoundStmt;
using clang::SwitchStmt;
using clang::ast_matchers::switchStmt;
using siliscope::switch_clause::Clause;
using siliscope::switch_clause::clauseEmpty;
using siliscope::switch_clause::clauseTerminates;
using siliscope::switch_clause::collectClauses;
using siliscope::switch_clause::hasFallthroughComment;

namespace {

constexpr const char *kId = "ss.ctrl.switch-fallthrough-comment";

} // namespace

void SwitchFallthroughCommentCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(switchStmt().bind("sw"), this);
}

void SwitchFallthroughCommentCheck::run(
    const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *sw = result.Nodes.getNodeAs<SwitchStmt>("sw");
  if (!sw || !result.SourceManager || !result.Context) {
    return;
  }
  const auto *body = llvm::dyn_cast_or_null<CompoundStmt>(sw->getBody());
  if (!body) {
    return;
  }
  llvm::SmallVector<Clause, 8> clauses;
  collectClauses(body, clauses);
  const clang::SourceManager &sm = *result.SourceManager;
  for (const Clause &clause : clauses) {
    // An empty clause is switch-well-formed's finding. A terminator, including
    // a fall-through attribute, already documents the jump.
    if (clauseEmpty(clause) || !clause.next || clauseTerminates(clause)) {
      continue;
    }
    if (hasFallthroughComment(clause, sm, result.Context->getLangOpts())) {
      continue;
    }
    reporter.emit(
        sm, clause.next->getBeginLoc(), kId, "fall-through to the next case needs a comment");
  }
}
