#include "siliscope/NoLogInIsr.h"

#include "siliscope/Isr.h"
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

struct Edge {
  const FunctionDecl *to;
  const CallExpr *call;
};

static bool isLogOrConsole(const FunctionDecl *fn) {
  if (!fn || !fn->getIdentifier()) {
    return false;
  }
  const llvm::StringRef n = fn->getName();
  if (n.contains("printf")) {
    return true;
  }
  return n == "puts" || n == "putchar" || n == "fputs" || n == "fwrite" || n == "scanf" ||
         n == "fscanf" || n == "sscanf" || n == "gets" || n == "fgets" || n == "perror";
}

class CallGraphVisitor : public clang::RecursiveASTVisitor<CallGraphVisitor> {
public:
  std::unordered_map<const FunctionDecl *, std::vector<Edge>> graph;
  std::vector<const FunctionDecl *> isrs;

  bool TraverseFunctionDecl(FunctionDecl *d) {
    const FunctionDecl *prev = current;
    if (d && d->isThisDeclarationADefinition() && !d->isImplicit()) {
      current = d->getCanonicalDecl();
      if (isIsr(current)) {
        isrs.push_back(current);
      }
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
    if (!callee) {
      return true;
    }
    graph[current].push_back({callee->getCanonicalDecl(), c});
    return true;
  }

private:
  const FunctionDecl *current = nullptr;
};

enum Reach { Unknown = 0, Visiting, No, Yes };

} // namespace

void NoLogInIsrCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void NoLogInIsrCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *tu = result.Nodes.getNodeAs<TranslationUnitDecl>("tu");
  if (!tu || !result.SourceManager) {
    return;
  }
  CallGraphVisitor v;
  v.TraverseDecl(const_cast<TranslationUnitDecl *>(tu));

  std::unordered_map<const FunctionDecl *, Reach> memo;
  const auto reaches = [&](auto &&self, const FunctionDecl *fn) -> bool {
    if (!fn) {
      return false;
    }
    if (isLogOrConsole(fn)) {
      return true;
    }
    const Reach st = memo[fn];
    if (st == Visiting || st == No) {
      return false;
    }
    if (st == Yes) {
      return true;
    }
    memo[fn] = Visiting;
    bool hit = false;
    const auto it = v.graph.find(fn);
    if (it != v.graph.end()) {
      for (const Edge &e : it->second) {
        if (self(self, e.to)) {
          hit = true;
          break;
        }
      }
    }
    memo[fn] = hit ? Yes : No;
    return hit;
  };

  for (const FunctionDecl *isr : v.isrs) {
    const auto it = v.graph.find(isr);
    if (it == v.graph.end()) {
      continue;
    }
    for (const Edge &e : it->second) {
      if (reaches(reaches, e.to)) {
        reporter.emit(*result.SourceManager,
                      e.call->getBeginLoc(),
                      "ss.emb.no-log-in-isr",
                      "do not format logs or call the console from an ISR");
      }
    }
  }
}
