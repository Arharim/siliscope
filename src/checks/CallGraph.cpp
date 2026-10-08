#include "siliscope/CallGraph.h"

#include "siliscope/Isr.h"
#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Linkage.h"
#include "clang/Basic/SourceManager.h"
#include "clang/UnifiedSymbolResolution/USRGeneration.h"
#include "llvm/ADT/SmallString.h"

#include <map>
#include <set>
#include <string>

using clang::CallExpr;
using clang::FunctionDecl;
using clang::Linkage;
using clang::SourceManager;
using clang::TranslationUnitDecl;
using clang::ast_matchers::translationUnitDecl;

namespace {

constexpr const char *kRecurse = "ss.ctrl.no-recursion";
constexpr const char *kIsrCall = "ss.emb.isr-not-called";
constexpr const char *kIsrLog = "ss.emb.no-log-in-isr";

struct Edge {
  std::string to;
  DiagSite at;
  bool operator<(const Edge &other) const {
    if (to != other.to) {
      return to < other.to;
    }
    if (at.file != other.at.file) {
      return at.file < other.at.file;
    }
    if (at.line != other.at.line) {
      return at.line < other.at.line;
    }
    return at.col < other.at.col;
  }
};

struct Node {
  std::set<Edge> out;
  bool isr = false;
  bool log = false;
};

bool isLogOrConsole(const FunctionDecl *fn) {
  if (!fn || !fn->getIdentifier()) {
    return false;
  }
  const llvm::StringRef name = fn->getName();
  if (name.contains("printf")) {
    return true;
  }
  return name == "puts" || name == "putchar" || name == "fputs" || name == "fwrite" ||
         name == "scanf" || name == "fscanf" || name == "sscanf" || name == "gets" ||
         name == "fgets" || name == "perror";
}

std::string keyOf(const FunctionDecl *fn, const SourceManager &sm) {
  if (!fn) {
    return {};
  }
  fn = fn->getCanonicalDecl();
  llvm::SmallString<256> buf;
  if (clang::index::generateUSRForDecl(fn, buf) || buf.empty()) {
    return {};
  }
  std::string key(buf);
  // USR of an external function matches across translation units. A static
  // with the same name in another file must not.
  if (fn->getFormalLinkage() == Linkage::Internal) {
    const clang::SourceLocation loc = sm.getSpellingLoc(fn->getLocation());
    const clang::PresumedLoc pl = sm.getPresumedLoc(loc);
    if (!pl.isInvalid() && pl.getFilename()) {
      key.push_back('@');
      key.append(pl.getFilename());
    }
  }
  return key;
}

enum Color { White, Gray, Black };
enum Reach { Unknown = 0, Visiting, No, Yes };

class Collector : public clang::RecursiveASTVisitor<Collector> {
public:
  Collector(std::map<std::string, Node> &nodes, const SourceManager &sm, Reporter &reporter)
      : nodes(nodes), sm(sm), reporter(reporter) {}

  // CXXMethodDecl and the constructor family do not go through TraverseFunctionDecl.
  template <typename DeclT, typename Walk>
  bool enter(DeclT *d, Walk &&walk) {
    const FunctionDecl *prev = current;
    if (d && d->isThisDeclarationADefinition() && !d->isImplicit()) {
      current = d->getCanonicalDecl();
      note(current);
    }
    const bool ok = walk();
    current = prev;
    return ok;
  }

  bool TraverseFunctionDecl(FunctionDecl *d) {
    return enter(d, [&] { return RecursiveASTVisitor::TraverseFunctionDecl(d); });
  }

  bool TraverseCXXMethodDecl(clang::CXXMethodDecl *d) {
    return enter(d, [&] { return RecursiveASTVisitor::TraverseCXXMethodDecl(d); });
  }

  bool TraverseCXXConstructorDecl(clang::CXXConstructorDecl *d) {
    return enter(d, [&] { return RecursiveASTVisitor::TraverseCXXConstructorDecl(d); });
  }

  bool TraverseCXXDestructorDecl(clang::CXXDestructorDecl *d) {
    return enter(d, [&] { return RecursiveASTVisitor::TraverseCXXDestructorDecl(d); });
  }

  bool TraverseCXXConversionDecl(clang::CXXConversionDecl *d) {
    return enter(d, [&] { return RecursiveASTVisitor::TraverseCXXConversionDecl(d); });
  }

  bool VisitCallExpr(CallExpr *call) {
    if (!current || !call) {
      return true;
    }
    const FunctionDecl *callee = call->getDirectCallee();
    if (!callee || callee->isImplicit()) {
      return true;
    }
    const std::string from = keyOf(current, sm);
    const std::string to = keyOf(callee, sm);
    if (from.empty() || to.empty()) {
      return true;
    }
    note(callee);
    DiagSite at;
    if (const std::optional<DiagSite> site = reporter.locate(sm, call->getBeginLoc())) {
      at = *site;
    }
    nodes[from].out.insert(Edge{to, at});
    return true;
  }

private:
  void note(const FunctionDecl *fn) {
    const std::string key = keyOf(fn, sm);
    if (key.empty()) {
      return;
    }
    Node &node = nodes[key];
    if (isIsr(fn)) {
      node.isr = true;
    }
    if (isLogOrConsole(fn)) {
      node.log = true;
    }
  }

  std::map<std::string, Node> &nodes;
  const SourceManager &sm;
  Reporter &reporter;
  const FunctionDecl *current = nullptr;
};

void reportRecursion(const std::map<std::string, Node> &nodes, Reporter &reporter) {
  std::map<std::string, Color> color;
  const auto dfs = [&](auto &&self, const std::string &id) -> void {
    color[id] = Gray;
    const auto it = nodes.find(id);
    if (it == nodes.end()) {
      color[id] = Black;
      return;
    }
    for (const Edge &edge : it->second.out) {
      const Color seen = color[edge.to];
      if (seen == Gray) {
        reporter.emitSite(edge.at, kRecurse, "do not recurse");
      } else if (seen == White) {
        self(self, edge.to);
      }
    }
    color[id] = Black;
  };
  for (const auto &kv : nodes) {
    if (color[kv.first] == White) {
      dfs(dfs, kv.first);
    }
  }
}

bool reachesLog(const std::map<std::string, Node> &nodes,
                std::map<std::string, Reach> &memo,
                const std::string &id) {
  const auto it = nodes.find(id);
  if (it != nodes.end() && it->second.log) {
    return true;
  }
  const Reach state = memo[id];
  if (state == Visiting || state == No) {
    return false;
  }
  if (state == Yes) {
    return true;
  }
  memo[id] = Visiting;
  bool hit = false;
  if (it != nodes.end()) {
    for (const Edge &edge : it->second.out) {
      if (reachesLog(nodes, memo, edge.to)) {
        hit = true;
        break;
      }
    }
  }
  memo[id] = hit ? Yes : No;
  return hit;
}

void reportIsr(const std::map<std::string, Node> &nodes, Reporter &reporter) {
  std::map<std::string, Reach> memo;
  for (const auto &kv : nodes) {
    for (const Edge &edge : kv.second.out) {
      const auto target = nodes.find(edge.to);
      if (target != nodes.end() && target->second.isr) {
        reporter.emitSite(edge.at, kIsrCall, "do not call an ISR like a normal function");
      }
    }
    if (!kv.second.isr) {
      continue;
    }
    for (const Edge &edge : kv.second.out) {
      if (reachesLog(nodes, memo, edge.to)) {
        reporter.emitSite(edge.at, kIsrLog, "do not format logs or call the console from an ISR");
      }
    }
  }
}

class Pass : public clang::ast_matchers::MatchFinder::MatchCallback {
public:
  Pass(ProgramCallGraph &graph, Reporter &reporter) : graph(graph), reporter(reporter) {}

  void run(const clang::ast_matchers::MatchFinder::MatchResult &result) override {
    const auto *tu = result.Nodes.getNodeAs<TranslationUnitDecl>("tu");
    if (!tu || !result.SourceManager) {
      return;
    }
    graph.record(const_cast<TranslationUnitDecl *>(tu), *result.SourceManager, reporter);
  }

private:
  ProgramCallGraph &graph;
  Reporter &reporter;
};

} // namespace

struct ProgramCallGraph::Data {
  std::map<std::string, Node> nodes;
};

ProgramCallGraph::ProgramCallGraph() : data(std::make_unique<Data>()) {}

ProgramCallGraph::~ProgramCallGraph() = default;

void ProgramCallGraph::record(TranslationUnitDecl *tu,
                              const SourceManager &sm,
                              Reporter &reporter) {
  if (!tu) {
    return;
  }
  Collector collector(data->nodes, sm, reporter);
  collector.TraverseDecl(tu);
}

void ProgramCallGraph::finish(Reporter &reporter) const {
  reportRecursion(data->nodes, reporter);
  reportIsr(data->nodes, reporter);
}

void attachCallGraph(clang::ast_matchers::MatchFinder &finder,
                     ProgramCallGraph &graph,
                     Reporter &reporter,
                     std::unique_ptr<clang::ast_matchers::MatchFinder::MatchCallback> &slot) {
  slot = std::make_unique<Pass>(graph, reporter);
  finder.addMatcher(translationUnitDecl().bind("tu"), slot.get());
}
