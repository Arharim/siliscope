#include "siliscope/Cyclomatic.h"

#include "siliscope/Report.h"

#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprConcepts.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>

using clang::BinaryOperator;
using clang::CaseStmt;
using clang::ConditionalOperator;
using clang::CXXCatchStmt;
using clang::CXXConstructorDecl;
using clang::CXXForRangeStmt;
using clang::DoStmt;
using clang::ForStmt;
using clang::FunctionDecl;
using clang::IfStmt;
using clang::Stmt;
using clang::WhileStmt;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

constexpr const char *kId = "ss.fn.cyclomatic";
constexpr const char *kMsg = "keep this function to 15 independent paths";
constexpr unsigned kLimit = 15;

bool allowed(const FunctionDecl *fn, Reporter &reporter) {
  if (!fn || fn->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = fn->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

// McCabe: one path, plus each if, loop, case, catch, &&, ||, and ?:.
unsigned decisions(const Stmt *stmt) {
  if (!stmt || llvm::isa<clang::LambdaExpr>(stmt) ||
      llvm::isa<clang::UnaryExprOrTypeTraitExpr>(stmt) || llvm::isa<clang::CXXNoexceptExpr>(stmt) ||
      llvm::isa<clang::CXXTypeidExpr>(stmt) || llvm::isa<clang::GenericSelectionExpr>(stmt) ||
      llvm::isa<clang::RequiresExpr>(stmt)) {
    return 0;
  }
  unsigned count = 0;
  if (llvm::isa<IfStmt>(stmt) || llvm::isa<ForStmt>(stmt) || llvm::isa<CXXForRangeStmt>(stmt) ||
      llvm::isa<WhileStmt>(stmt) || llvm::isa<DoStmt>(stmt) || llvm::isa<CaseStmt>(stmt) ||
      llvm::isa<CXXCatchStmt>(stmt) || llvm::isa<ConditionalOperator>(stmt) ||
      llvm::isa<clang::BinaryConditionalOperator>(stmt)) {
    count = 1;
  } else if (const auto *binary = llvm::dyn_cast<BinaryOperator>(stmt)) {
    if (binary->isLogicalOp()) {
      count = 1;
    }
  }
  for (const Stmt *child : stmt->children()) {
    count += decisions(child);
  }
  return count;
}

unsigned paths(const FunctionDecl *fn) {
  unsigned count = 1;
  if (const auto *ctor = llvm::dyn_cast<CXXConstructorDecl>(fn)) {
    for (const clang::CXXCtorInitializer *init : ctor->inits()) {
      if (!init->isWritten()) {
        continue;
      }
      if (const clang::Expr *expr = init->getInit()) {
        count += decisions(expr);
      }
    }
  }
  if (const Stmt *body = fn->getBody()) {
    count += decisions(body);
  }
  return count;
}

} // namespace

void CyclomaticCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
}

void CyclomaticCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !fn->getBody() || !result.SourceManager || fn->isTemplateInstantiation() ||
      allowed(fn, reporter)) {
    return;
  }
  if (paths(fn) <= kLimit) {
    return;
  }
  reporter.emit(*result.SourceManager, fn->getLocation(), kId, kMsg);
}
