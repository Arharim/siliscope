#include "siliscope/NoThrowingSwap.h"

#include "siliscope/Report.h"

#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/ExceptionSpecificationType.h"

using clang::CallExpr;
using clang::CXXMethodDecl;
using clang::CXXNewExpr;
using clang::ExceptionSpecificationType;
using clang::Expr;
using clang::FunctionDecl;
using clang::FunctionProtoType;
using clang::Stmt;
using clang::ast_matchers::cxxMethodDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

bool specCanThrow(const FunctionDecl *fn) {
  const auto *proto = fn->getType()->getAs<FunctionProtoType>();
  if (!proto) {
    return false;
  }
  switch (proto->getExceptionSpecType()) {
  case clang::EST_None:
  case clang::EST_NoexceptFalse:
  case clang::EST_MSAny:
    return true;
  case clang::EST_Dynamic:
    return proto->getNumExceptions() > 0;
  default:
    return false;
  }
}

class AllocVisitor : public clang::RecursiveASTVisitor<AllocVisitor> {
public:
  bool alloc = false;

  bool VisitCXXNewExpr(CXXNewExpr *) {
    alloc = true;
    return true;
  }

  bool VisitCallExpr(CallExpr *call) {
    const FunctionDecl *callee = call ? call->getDirectCallee() : nullptr;
    // operator new and other overloaded operators are not identifiers.
    if (!callee || !callee->getIdentifier()) {
      return true;
    }
    const llvm::StringRef name = callee->getName();
    if (name == "malloc" || name == "calloc" || name == "realloc" || name == "aligned_alloc") {
      alloc = true;
    }
    return true;
  }
};

bool bodyAllocates(const FunctionDecl *fn) {
  const FunctionDecl *def = fn->getDefinition();
  if (!def || !def->getBody()) {
    return false;
  }
  AllocVisitor visitor;
  visitor.TraverseStmt(const_cast<Stmt *>(def->getBody()));
  return visitor.alloc;
}

class SwapCalls : public clang::RecursiveASTVisitor<SwapCalls> {
public:
  explicit SwapCalls(const clang::SourceManager &sm, Reporter &reporter)
      : sm(sm), reporter(reporter) {}

  bool VisitCallExpr(CallExpr *call) {
    const FunctionDecl *callee = call ? call->getDirectCallee() : nullptr;
    if (!callee || !callee->getIdentifier() || callee->getName() != "swap") {
      return true;
    }
    if (specCanThrow(callee)) {
      reporter.emit(sm,
                    call->getBeginLoc(),
                    "ss.cpp.no-throwing-swap",
                    "a swap used by assignment shall be noexcept");
    }
    if (bodyAllocates(callee)) {
      reporter.emit(sm,
                    call->getBeginLoc(),
                    "ss.cpp.no-throwing-swap",
                    "a swap used by assignment shall not allocate");
    }
    return true;
  }

private:
  const clang::SourceManager &sm;
  Reporter &reporter;
};

} // namespace

void NoThrowingSwapCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(cxxMethodDecl(isDefinition(), unless(isImplicit())).bind("op"), this);
}

void NoThrowingSwapCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.Context->getLangOpts().CPlusPlus || !result.SourceManager) {
    return;
  }
  const auto *op = result.Nodes.getNodeAs<CXXMethodDecl>("op");
  // =default does not call swap. Only a written copy or move assignment does.
  if (!op || !op->isUserProvided() || !op->getBody() ||
      (!op->isCopyAssignmentOperator() && !op->isMoveAssignmentOperator())) {
    return;
  }
  // A library assignment is not the user's construct-and-swap. Its body also
  // calls overloaded operators, whose names are not identifiers.
  if (result.SourceManager->isInSystemHeader(op->getBody()->getBeginLoc())) {
    return;
  }
  SwapCalls calls(*result.SourceManager, reporter);
  calls.TraverseStmt(const_cast<Stmt *>(op->getBody()));
}
