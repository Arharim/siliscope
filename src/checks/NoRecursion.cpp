#include "siliscope/NoRecursion.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include <unordered_map>
#include <vector>

using clang::CallExpr;
using clang::FunctionDecl;
using clang::TranslationUnitDecl;
using clang::ast_matchers::translationUnitDecl;

namespace {

enum Color { White, Gray, Black };

struct Edge {
  const FunctionDecl *to;
  const CallExpr *call;
};

class CallGraphVisitor : public clang::RecursiveASTVisitor<CallGraphVisitor> {
public:
  std::unordered_map<const FunctionDecl *, std::vector<Edge>> graph;

  bool TraverseFunctionDecl(FunctionDecl *d) {
    const FunctionDecl *prev = current;
    if (d && d->isThisDeclarationADefinition() && !d->isImplicit()) {
      current = d->getCanonicalDecl();
    }
    const bool ok = RecursiveASTVisitor::TraverseFunctionDecl(d);
    current = prev;
    return ok;
  }

  bool VisitCallExpr(CallExpr *c) {
    if (!current || !c) {
      return true;
    }
    const FunctionDecl *callee = c->getDirectCallee();
    if (!callee || callee->isImplicit() || callee->getBuiltinID() != 0) {
      return true;
    }
    graph[current].push_back({callee->getCanonicalDecl(), c});
    return true;
  }

private:
  const FunctionDecl *current = nullptr;
};

} // namespace

void NoRecursionCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void NoRecursionCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *tu = result.Nodes.getNodeAs<TranslationUnitDecl>("tu");
  if (!tu || !result.SourceManager) {
    return;
  }
  CallGraphVisitor v;
  v.TraverseDecl(const_cast<TranslationUnitDecl *>(tu));

  std::unordered_map<const FunctionDecl *, Color> color;
  const auto dfs = [&](auto &&self, const FunctionDecl *u) -> void {
    color[u] = Gray;
    for (const Edge &e : v.graph[u]) {
      const Color c = color[e.to];
      if (c == Gray) {
        reporter.emit(
            *result.SourceManager, e.call->getBeginLoc(), "ss.ctrl.no-recursion", "do not recurse");
      } else if (c == White) {
        self(self, e.to);
      }
    }
    color[u] = Black;
  };

  for (const auto &kv : v.graph) {
    if (color[kv.first] == White) {
      dfs(dfs, kv.first);
    }
  }
}
