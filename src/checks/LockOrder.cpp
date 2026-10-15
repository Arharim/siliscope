#include "siliscope/LockOrder.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

using clang::CallExpr;
using clang::CompoundStmt;
using clang::DeclRefExpr;
using clang::DoStmt;
using clang::Expr;
using clang::ForStmt;
using clang::FunctionDecl;
using clang::IfStmt;
using clang::SourceLocation;
using clang::Stmt;
using clang::SwitchStmt;
using clang::TranslationUnitDecl;
using clang::VarDecl;
using clang::WhileStmt;
using clang::ast_matchers::translationUnitDecl;

namespace {

constexpr const char *kId = "ss.conc.lock-order";
constexpr const char *kMsg = "this lock nests in the opposite order elsewhere";

bool allowed(const FunctionDecl *fn, Reporter &reporter) {
  if (!fn || !fn->getDeclName().isIdentifier()) {
    return false;
  }
  const std::string name = fn->getName().str();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

bool isAcquire(llvm::StringRef name) {
  if (name.contains("FromISR") || name.contains("FROM_ISR") || name.contains("from_isr")) {
    return false;
  }
  if (name.contains_insensitive("unlock") || name.contains_insensitive("clock")) {
    return false;
  }
  if (name.contains("ENTER_CRITICAL") || name.contains("EnterCritical") || name == "irq_lock" ||
      name.ends_with("disable_irq")) {
    return true;
  }
  if (name.contains_insensitive("mutex") &&
      (name.contains_insensitive("take") || name.contains_insensitive("acquire") ||
       name.contains_insensitive("lock"))) {
    return true;
  }
  if (name.contains("SemaphoreTake")) {
    return true;
  }
  return name == "lock" || name.starts_with("lock_") || name.starts_with("Lock") ||
         name.ends_with("_lock") || name.ends_with("Lock");
}

bool isRelease(llvm::StringRef name) {
  if (name.contains("EXIT_CRITICAL") || name.contains("ExitCritical") || name == "irq_unlock" ||
      name.ends_with("enable_irq")) {
    return true;
  }
  if (name.contains_insensitive("unlock")) {
    return true;
  }
  if (name.contains("SemaphoreGive") || name.contains("MutexGive")) {
    return true;
  }
  return name.contains_insensitive("mutex") && name.contains_insensitive("give");
}

std::string lockId(const CallExpr *call, llvm::StringRef callee) {
  if (call->getNumArgs() > 0 && call->getArg(0)) {
    const Expr *arg = call->getArg(0)->IgnoreParenImpCasts();
    if (const auto *ref = llvm::dyn_cast<DeclRefExpr>(arg)) {
      if (const auto *var = llvm::dyn_cast<VarDecl>(ref->getDecl())) {
        if (var->getDeclName().isIdentifier() && !var->getName().empty()) {
          return "obj:" + var->getCanonicalDecl()->getName().str();
        }
      }
    }
  }
  return "fn:" + callee.str();
}

struct Nest {
  std::string outer;
  std::string inner;
  SourceLocation at;
  const FunctionDecl *fn = nullptr;
};

// Straight-line nest inside one function. A lock taken in a branch does not
// stay held after that branch. Pairs are compared across the whole file.
void walk(const Stmt *stmt,
          std::vector<std::string> &held,
          std::vector<Nest> &nests,
          const FunctionDecl *fn) {
  if (!stmt) {
    return;
  }
  if (const auto *compound = llvm::dyn_cast<CompoundStmt>(stmt)) {
    for (const Stmt *child : compound->body()) {
      walk(child, held, nests, fn);
    }
    return;
  }
  if (const auto *ifs = llvm::dyn_cast<IfStmt>(stmt)) {
    walk(ifs->getInit(), held, nests, fn);
    walk(ifs->getCond(), held, nests, fn);
    std::vector<std::string> thenHeld = held;
    walk(ifs->getThen(), thenHeld, nests, fn);
    std::vector<std::string> elseHeld = held;
    walk(ifs->getElse(), elseHeld, nests, fn);
    return;
  }
  if (const auto *loop = llvm::dyn_cast<WhileStmt>(stmt)) {
    walk(loop->getCond(), held, nests, fn);
    std::vector<std::string> body = held;
    walk(loop->getBody(), body, nests, fn);
    return;
  }
  if (const auto *loop = llvm::dyn_cast<DoStmt>(stmt)) {
    std::vector<std::string> body = held;
    walk(loop->getBody(), body, nests, fn);
    walk(loop->getCond(), held, nests, fn);
    return;
  }
  if (const auto *loop = llvm::dyn_cast<ForStmt>(stmt)) {
    walk(loop->getInit(), held, nests, fn);
    walk(loop->getCond(), held, nests, fn);
    std::vector<std::string> body = held;
    walk(loop->getBody(), body, nests, fn);
    walk(loop->getInc(), body, nests, fn);
    return;
  }
  if (const auto *sw = llvm::dyn_cast<SwitchStmt>(stmt)) {
    walk(sw->getCond(), held, nests, fn);
    std::vector<std::string> body = held;
    walk(sw->getBody(), body, nests, fn);
    return;
  }
  if (const auto *call = llvm::dyn_cast<CallExpr>(stmt)) {
    for (const Expr *arg : call->arguments()) {
      walk(arg, held, nests, fn);
    }
    const FunctionDecl *callee = call->getDirectCallee();
    if (!callee || !callee->getDeclName().isIdentifier()) {
      return;
    }
    const llvm::StringRef name = callee->getName();
    const bool acquire = isAcquire(name);
    if (!acquire && !isRelease(name)) {
      return;
    }
    const std::string id = lockId(call, name);
    if (acquire) {
      for (const std::string &outer : held) {
        if (outer != id) {
          nests.push_back({outer, id, call->getExprLoc(), fn});
        }
      }
      held.push_back(id);
      return;
    }
    for (std::size_t i = held.size(); i > 0; --i) {
      if (held[i - 1] == id) {
        held.erase(held.begin() + static_cast<std::ptrdiff_t>(i - 1));
        return;
      }
    }
    return;
  }
  for (const Stmt *child : stmt->children()) {
    walk(child, held, nests, fn);
  }
}

class FindFunctions : public clang::RecursiveASTVisitor<FindFunctions> {
public:
  std::vector<Nest> nests;

  bool VisitFunctionDecl(FunctionDecl *fn) {
    if (!fn || !fn->isThisDeclarationADefinition() || !fn->getBody() || fn->isImplicit() ||
        fn->isTemplateInstantiation()) {
      return true;
    }
    std::vector<std::string> held;
    walk(fn->getBody(), held, nests, fn);
    return true;
  }
};

} // namespace

void LockOrderCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void LockOrderCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *tu = result.Nodes.getNodeAs<TranslationUnitDecl>("tu");
  if (!tu || !result.SourceManager) {
    return;
  }
  FindFunctions found;
  found.TraverseDecl(const_cast<TranslationUnitDecl *>(tu));
  std::set<std::pair<std::string, std::string>> orders;
  for (const Nest &nest : found.nests) {
    orders.insert({nest.outer, nest.inner});
  }
  for (const Nest &nest : found.nests) {
    if (!orders.count({nest.inner, nest.outer}) || allowed(nest.fn, reporter)) {
      continue;
    }
    reporter.emit(*result.SourceManager, nest.at, kId, kMsg);
  }
}
