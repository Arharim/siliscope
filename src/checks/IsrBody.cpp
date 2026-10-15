#include "siliscope/IsrBody.h"

#include "siliscope/Isr.h"
#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>

using clang::CallExpr;
using clang::CXXBoolLiteralExpr;
using clang::CXXDeleteExpr;
using clang::CXXNewExpr;
using clang::DoStmt;
using clang::Expr;
using clang::ForStmt;
using clang::FunctionDecl;
using clang::IntegerLiteral;
using clang::WhileStmt;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

constexpr const char *kId = "ss.emb.isr-body";
constexpr const char *kBlock = "do not block in an interrupt handler";
constexpr const char *kLoop = "an interrupt handler has no unbounded loop";

bool allowed(const FunctionDecl *fn, Reporter &reporter) {
  if (!fn || !fn->getDeclName().isIdentifier()) {
    return false;
  }
  const std::string name = fn->getName().str();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

bool fromIsr(llvm::StringRef name) {
  return name.contains("FromISR") || name.contains("FROM_ISR") || name.contains("from_isr");
}

std::string fold(llvm::StringRef name) {
  std::string out;
  out.reserve(name.size());
  for (char c : name) {
    if (c == '_') {
      continue;
    }
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
    out.push_back(c);
  }
  return out;
}

bool blocks(const FunctionDecl *fn) {
  if (!fn) {
    return false;
  }
  if (fn->getDeclName().getNameKind() == clang::DeclarationName::CXXOperatorName) {
    const clang::OverloadedOperatorKind op = fn->getDeclName().getCXXOverloadedOperator();
    return op == clang::OO_New || op == clang::OO_Array_New || op == clang::OO_Delete ||
           op == clang::OO_Array_Delete;
  }
  if (!fn->getDeclName().isIdentifier()) {
    return false;
  }
  const llvm::StringRef name = fn->getName();
  if (fromIsr(name)) {
    return false;
  }
  if (name == "malloc" || name == "calloc" || name == "realloc" || name == "aligned_alloc" ||
      name == "posix_memalign" || name == "free") {
    return true;
  }
  if (name.contains("Delay") || name.contains("delay") || name.contains("Sleep") ||
      name.contains("sleep")) {
    return true;
  }
  if (name.contains("QueueReceive") || name.contains("QueuePeek") ||
      name.contains("SemaphoreTake") || name.contains("SemaphoreAcquire") ||
      name.contains("FlagsWait") || name.contains("NotifyTake") || name.contains("NotifyWait") ||
      name.contains("MessageQueueGet")) {
    return true;
  }
  const std::string folded = fold(name);
  if (folded == "ksemtake" || folded == "kmsgqget" || folded == "kqueueget" || folded == "kpoll" ||
      folded == "ossemaphorewait" || folded == "osmessageget" || folded == "ossignalwait") {
    return true;
  }
  return folded.find("get") == std::string::npos && folded.find("flash") != std::string::npos &&
         folded.find("erase") != std::string::npos;
}

bool literalTrue(const Expr *expr) {
  if (!expr) {
    return false;
  }
  expr = expr->IgnoreParenImpCasts();
  if (const auto *lit = llvm::dyn_cast<IntegerLiteral>(expr)) {
    return lit->getValue().isOne();
  }
  if (const auto *boolean = llvm::dyn_cast<CXXBoolLiteralExpr>(expr)) {
    return boolean->getValue();
  }
  return false;
}

class FindBody : public clang::RecursiveASTVisitor<FindBody> {
public:
  explicit FindBody(Reporter &reporter, const clang::SourceManager &sm)
      : reporter(reporter), sm(sm) {}

  bool VisitCallExpr(CallExpr *call) {
    if (call && blocks(call->getDirectCallee())) {
      reporter.emit(sm, call->getExprLoc(), kId, kBlock);
    }
    return true;
  }

  bool VisitCXXNewExpr(CXXNewExpr *expr) {
    if (expr) {
      reporter.emit(sm, expr->getExprLoc(), kId, kBlock);
    }
    return true;
  }

  bool VisitCXXDeleteExpr(CXXDeleteExpr *expr) {
    if (expr) {
      reporter.emit(sm, expr->getExprLoc(), kId, kBlock);
    }
    return true;
  }

  bool VisitForStmt(ForStmt *loop) {
    if (loop && !loop->getCond()) {
      reporter.emit(sm, loop->getForLoc(), kId, kLoop);
    }
    return true;
  }

  bool VisitWhileStmt(WhileStmt *loop) {
    if (loop && literalTrue(loop->getCond())) {
      reporter.emit(sm, loop->getWhileLoc(), kId, kLoop);
    }
    return true;
  }

  bool VisitDoStmt(DoStmt *loop) {
    if (loop && literalTrue(loop->getCond())) {
      reporter.emit(sm, loop->getDoLoc(), kId, kLoop);
    }
    return true;
  }

private:
  Reporter &reporter;
  const clang::SourceManager &sm;
};

} // namespace

void IsrBodyCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), this);
}

void IsrBodyCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
  if (!fn || !fn->getBody() || !result.SourceManager || !isIsr(fn) ||
      fn->isTemplateInstantiation() || allowed(fn, reporter)) {
    return;
  }
  FindBody walk(reporter, *result.SourceManager);
  walk.TraverseStmt(fn->getBody());
}
