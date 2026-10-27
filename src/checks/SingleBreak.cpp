#include "siliscope/SingleBreak.h"

#include "siliscope/Report.h"

#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <string>
#include <vector>

using clang::ASTContext;
using clang::BreakStmt;
using clang::DoStmt;
using clang::DynTypedNode;
using clang::ForStmt;
using clang::FunctionDecl;
using clang::GotoStmt;
using clang::LabelStmt;
using clang::Stmt;
using clang::SwitchStmt;
using clang::WhileStmt;
using clang::ast_matchers::cxxForRangeStmt;
using clang::ast_matchers::doStmt;
using clang::ast_matchers::forStmt;
using clang::ast_matchers::whileStmt;

namespace {

constexpr const char *kId = "ss.ctrl.single-break-in-loop";
constexpr const char *kMsg = "give this loop at most one break or goto";

const FunctionDecl *enclosing(const Stmt *stmt, ASTContext &ctx) {
  DynTypedNode node = DynTypedNode::create(*stmt);
  for (int step = 0; step < 64; ++step) {
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      return nullptr;
    }
    node = parents[0];
    if (const auto *fn = node.get<FunctionDecl>()) {
      return fn;
    }
  }
  return nullptr;
}

// Breaks that leave this loop, and gotos whose label is outside it. A break
// inside a switch stays with the switch. A nested loop keeps its own count.
class Jumps : public clang::RecursiveASTVisitor<Jumps> {
public:
  bool VisitBreakStmt(BreakStmt *stmt) {
    if (switches == 0) {
      jumps.push_back(stmt);
    }
    return true;
  }

  bool VisitGotoStmt(GotoStmt *stmt) {
    gotos.push_back(stmt);
    return true;
  }

  bool VisitLabelStmt(LabelStmt *stmt) {
    labels.push_back(stmt);
    return true;
  }

  bool TraverseForStmt(ForStmt *) { return true; }
  bool TraverseWhileStmt(WhileStmt *) { return true; }
  bool TraverseDoStmt(DoStmt *) { return true; }
  bool TraverseCXXForRangeStmt(clang::CXXForRangeStmt *) { return true; }
  bool TraverseLambdaExpr(clang::LambdaExpr *) { return true; }

  bool TraverseSwitchStmt(SwitchStmt *stmt) {
    ++switches;
    const bool ok = RecursiveASTVisitor::TraverseSwitchStmt(stmt);
    --switches;
    return ok;
  }

  std::vector<const Stmt *> leaving() const {
    std::vector<const Stmt *> out = jumps;
    for (const GotoStmt *gotoStmt : gotos) {
      const LabelStmt *label = gotoStmt->getLabel() ? gotoStmt->getLabel()->getStmt() : nullptr;
      bool inside = false;
      for (const LabelStmt *have : labels) {
        if (have == label) {
          inside = true;
          break;
        }
      }
      if (!inside) {
        out.push_back(gotoStmt);
      }
    }
    return out;
  }

private:
  int switches = 0;
  std::vector<const Stmt *> jumps;
  std::vector<const GotoStmt *> gotos;
  std::vector<const LabelStmt *> labels;
};

void check(const Stmt *loop, const Stmt *body, ASTContext &ctx, Reporter &reporter) {
  if (!loop || !body) {
    return;
  }
  const FunctionDecl *fn = enclosing(loop, ctx);
  if (fn && !fn->getDeclName().isEmpty()) {
    const std::string name = fn->getDeclName().getAsString();
    if (!name.empty() && reporter.allows(kId, name.c_str())) {
      return;
    }
  }
  Jumps jumps;
  jumps.TraverseStmt(const_cast<Stmt *>(body));
  const std::vector<const Stmt *> leaving = jumps.leaving();
  if (leaving.size() <= 1) {
    return;
  }
  for (size_t i = 1; i < leaving.size(); ++i) {
    reporter.emit(ctx.getSourceManager(), leaving[i]->getBeginLoc(), kId, kMsg);
  }
}

} // namespace

void SingleBreakCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(forStmt().bind("for"), this);
  finder.addMatcher(whileStmt().bind("while"), this);
  finder.addMatcher(doStmt().bind("do"), this);
  finder.addMatcher(cxxForRangeStmt().bind("range"), this);
}

void SingleBreakCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context) {
    return;
  }
  if (const auto *loop = result.Nodes.getNodeAs<ForStmt>("for")) {
    check(loop, loop->getBody(), *result.Context, reporter);
  } else if (const auto *loop = result.Nodes.getNodeAs<WhileStmt>("while")) {
    check(loop, loop->getBody(), *result.Context, reporter);
  } else if (const auto *loop = result.Nodes.getNodeAs<DoStmt>("do")) {
    check(loop, loop->getBody(), *result.Context, reporter);
  } else if (const auto *loop = result.Nodes.getNodeAs<clang::CXXForRangeStmt>("range")) {
    check(loop, loop->getBody(), *result.Context, reporter);
  }
}
