#include "siliscope/Braces.h"

#include "siliscope/Report.h"

#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::DoStmt;
using clang::ForStmt;
using clang::IfStmt;
using clang::SwitchStmt;
using clang::WhileStmt;
using clang::ast_matchers::compoundStmt;
using clang::ast_matchers::doStmt;
using clang::ast_matchers::forStmt;
using clang::ast_matchers::hasBody;
using clang::ast_matchers::hasElse;
using clang::ast_matchers::hasThen;
using clang::ast_matchers::ifStmt;
using clang::ast_matchers::stmt;
using clang::ast_matchers::switchStmt;
using clang::ast_matchers::unless;
using clang::ast_matchers::whileStmt;

void BracesCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(ifStmt(unless(hasThen(compoundStmt()))).bind("if"), this);
  finder.addMatcher(ifStmt(hasElse(stmt(unless(compoundStmt()), unless(ifStmt())))).bind("else"),
                    this);
  finder.addMatcher(forStmt(unless(hasBody(compoundStmt()))).bind("for"), this);
  finder.addMatcher(whileStmt(unless(hasBody(compoundStmt()))).bind("while"), this);
  finder.addMatcher(doStmt(unless(hasBody(compoundStmt()))).bind("do"), this);
  finder.addMatcher(switchStmt().bind("switch"), this);
}

void BracesCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  clang::SourceLocation loc;
  if (const auto *s = result.Nodes.getNodeAs<IfStmt>("if")) {
    loc = s->getIfLoc();
  } else if (const auto *s = result.Nodes.getNodeAs<IfStmt>("else")) {
    loc = s->getElseLoc();
  } else if (const auto *s = result.Nodes.getNodeAs<ForStmt>("for")) {
    loc = s->getForLoc();
  } else if (const auto *s = result.Nodes.getNodeAs<WhileStmt>("while")) {
    loc = s->getWhileLoc();
  } else if (const auto *s = result.Nodes.getNodeAs<DoStmt>("do")) {
    loc = s->getDoLoc();
  } else if (const auto *s = result.Nodes.getNodeAs<SwitchStmt>("switch")) {
    if (llvm::isa_and_nonnull<clang::CompoundStmt>(s->getBody())) {
      return;
    }
    loc = s->getSwitchLoc();
  } else {
    return;
  }
  reporter.emit(*result.SourceManager, loc, "ss.ctrl.braces", "brace the body of this statement");
}
