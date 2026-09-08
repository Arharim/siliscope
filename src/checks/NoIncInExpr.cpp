#include "siliscope/NoIncInExpr.h"

#include "siliscope/Report.h"

#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using clang::ForStmt;
using clang::Stmt;
using clang::UnaryOperator;
using clang::ast_matchers::anyOf;
using clang::ast_matchers::hasOperatorName;
using clang::ast_matchers::unaryOperator;

static bool isSugar(const Stmt *s) {
  return llvm::isa<clang::ParenExpr>(s) || llvm::isa<clang::ImplicitCastExpr>(s) ||
         llvm::isa<clang::FullExpr>(s) || llvm::isa<clang::ConstantExpr>(s) ||
         llvm::isa<clang::ExprWithCleanups>(s);
}

void NoIncInExprCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(unaryOperator(anyOf(hasOperatorName("++"), hasOperatorName("--"))).bind("inc"),
                    this);
}

void NoIncInExprCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *op = result.Nodes.getNodeAs<UnaryOperator>("inc");
  if (!op || !result.Context) {
    return;
  }
  const Stmt *cur = op;
  for (;;) {
    const auto parents = result.Context->getParents(*cur);
    if (parents.empty()) {
      return;
    }
    const Stmt *parent = parents[0].get<Stmt>();
    if (!parent) {
      break;
    }
    if (isSugar(parent)) {
      cur = parent;
      continue;
    }
    // Clang 19: an expression-statement is the Expr itself under CompoundStmt
    // (no ExprStmt wrapper).
    if (llvm::isa<clang::CompoundStmt>(parent) || llvm::isa<clang::LabelStmt>(parent) ||
        llvm::isa<clang::CaseStmt>(parent) || llvm::isa<clang::DefaultStmt>(parent)) {
      return;
    }
    if (const auto *fs = llvm::dyn_cast<ForStmt>(parent)) {
      const clang::Expr *inc = fs->getInc();
      if (inc && inc->IgnoreParenImpCasts() == op) {
        return;
      }
    }
    break;
  }
  reporter.emit(*result.SourceManager,
                op->getOperatorLoc(),
                "ss.expr.no-inc-in-expr",
                "++/-- must be a standalone statement");
}
