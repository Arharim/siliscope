#include "siliscope/CallGraph.h"

#include "siliscope/FunctionKey.h"
#include "siliscope/Isr.h"
#include "siliscope/Report.h"
#include "siliscope/Section.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/StringRef.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using clang::CallExpr;
using clang::CFG;
using clang::CFGBlock;
using clang::CFGStmt;
using clang::CXXConstructExpr;
using clang::CXXDeleteExpr;
using clang::CXXNewExpr;
using clang::FunctionDecl;
using clang::SourceLocation;
using clang::SourceManager;
using clang::Stmt;
using clang::TranslationUnitDecl;
using clang::ast_matchers::translationUnitDecl;

namespace {

constexpr const char *kRecurse = "ss.ctrl.no-recursion";
constexpr const char *kIsrCall = "ss.emb.isr-not-called";
constexpr const char *kIsrLog = "ss.emb.no-log-in-isr";
constexpr const char *kBlock = "ss.conc.no-block-in-cs";
constexpr const char *kHeap = "ss.mem.no-heap-after-init";
constexpr const char *kBlockMsg = "do not block while a critical section or interrupt mask is held";
constexpr const char *kHeapMsg = "do not use the heap";
// Virtual caller for file-scope initializers. They run before main.
constexpr const char *kStaticInit = "<static-init>";
constexpr const char *kOperatorNew = "<operator new>";

struct CallKey {
  std::string to;
  DiagSite at;
  bool operator<(const CallKey &other) const {
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
  // The bool is true when this call happens while a critical section or an
  // interrupt mask is still open in the caller.
  std::map<CallKey, bool> out;
  std::vector<DiagSite> heap;
  std::string name;
  bool isr = false;
  bool log = false;
  bool block = false;
};

struct MaskDepth {
  int cs = 0;
  int irq = 0;
  int fault = 0;
  int basepri = 0;

  bool operator==(const MaskDepth &o) const {
    return cs == o.cs && irq == o.irq && fault == o.fault && basepri == o.basepri;
  }
  bool operator!=(const MaskDepth &o) const { return !(*this == o); }
  bool held() const { return cs > 0 || irq > 0 || fault > 0 || basepri > 0; }
};

enum class HeapKind { No, Alloc, Release };

bool isFromIsr(llvm::StringRef n) {
  return n.contains("FromISR") || n.contains("FROM_ISR") || n.contains("from_isr");
}

std::string fold(llvm::StringRef n) {
  std::string out;
  out.reserve(n.size());
  for (char c : n) {
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

bool isDelay(llvm::StringRef n) {
  if (isFromIsr(n)) {
    return false;
  }
  return n.contains("Delay") || n.contains("delay") || n.contains("Sleep") || n.contains("sleep");
}

bool isQueueWait(llvm::StringRef n) {
  if (isFromIsr(n)) {
    return false;
  }
  if (n.contains("QueueReceive") || n.contains("QueuePeek") || n.contains("SemaphoreTake") ||
      n.contains("SemaphoreAcquire") || n.contains("FlagsWait") || n.contains("NotifyTake") ||
      n.contains("NotifyWait") || n.contains("MessageQueueGet")) {
    return true;
  }
  const std::string f = fold(n);
  return f == "ksemtake" || f == "kmsgqget" || f == "kqueueget" || f == "kpoll" ||
         f == "ossemaphorewait" || f == "osmessageget" || f == "ossignalwait";
}

bool isFlashErase(llvm::StringRef n) {
  const std::string f = fold(n);
  if (f.find("get") != std::string::npos) {
    return false;
  }
  return f.find("flash") != std::string::npos && f.find("erase") != std::string::npos;
}

HeapKind heapKind(const FunctionDecl *fn) {
  if (!fn) {
    return HeapKind::No;
  }
  if (fn->getDeclName().getNameKind() == clang::DeclarationName::CXXOperatorName) {
    switch (fn->getDeclName().getCXXOverloadedOperator()) {
    case clang::OO_New:
    case clang::OO_Array_New:
      return HeapKind::Alloc;
    case clang::OO_Delete:
    case clang::OO_Array_Delete:
      return HeapKind::Release;
    default:
      return HeapKind::No;
    }
  }
  if (!fn->getIdentifier()) {
    return HeapKind::No;
  }
  const llvm::StringRef n = fn->getName();
  if (n == "free") {
    return HeapKind::Release;
  }
  if (n == "malloc" || n == "calloc" || n == "realloc" || n == "aligned_alloc" ||
      n == "posix_memalign") {
    return HeapKind::Alloc;
  }
  return HeapKind::No;
}

bool isBlocker(const FunctionDecl *fn) {
  if (!fn) {
    return false;
  }
  if (heapKind(fn) == HeapKind::Alloc) {
    return true;
  }
  if (!fn->getIdentifier()) {
    return false;
  }
  const llvm::StringRef n = fn->getName();
  return isDelay(n) || isQueueWait(n) || isFlashErase(n);
}

std::string nameOf(const FunctionDecl *fn) {
  if (!fn) {
    return {};
  }
  if (fn->getIdentifier()) {
    return fn->getName().str();
  }
  return fn->getNameAsString();
}

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
  return functionKey(fn, sm);
}

enum Color { White, Gray, Black };
enum Reach { Unknown = 0, Visiting, No, Yes };

void adjustMask(MaskDepth &d, const FunctionDecl *fn) {
  switch (siliscope::sectionOp(fn)) {
  case siliscope::SectionOp::Enter:
    ++d.cs;
    break;
  case siliscope::SectionOp::Leave:
  case siliscope::SectionOp::Restore:
    if (d.cs > 0) {
      --d.cs;
    }
    break;
  case siliscope::SectionOp::None:
    break;
  }
  switch (siliscope::irqOp(fn)) {
  case siliscope::IrqOp::BlindEnable:
    break;
  case siliscope::IrqOp::DisableIrq:
    ++d.irq;
    break;
  case siliscope::IrqOp::RestorePrimask:
    if (d.irq > 0) {
      --d.irq;
    }
    break;
  case siliscope::IrqOp::DisableFault:
    ++d.fault;
    break;
  case siliscope::IrqOp::RestoreFault:
    if (d.fault > 0) {
      --d.fault;
    }
    break;
  case siliscope::IrqOp::RaiseBasepri:
    ++d.basepri;
    break;
  case siliscope::IrqOp::SetBasepri:
    if (d.basepri > 0) {
      --d.basepri;
    } else {
      ++d.basepri;
    }
    break;
  case siliscope::IrqOp::None:
    break;
  }
}

bool touchesMask(const Stmt *s) {
  if (!s) {
    return false;
  }
  if (const auto *call = llvm::dyn_cast<CallExpr>(s)) {
    const FunctionDecl *fn = call->getDirectCallee();
    if (siliscope::sectionOp(fn) != siliscope::SectionOp::None ||
        siliscope::irqOp(fn) != siliscope::IrqOp::None) {
      return true;
    }
  }
  for (const Stmt *ch : s->children()) {
    if (touchesMask(ch)) {
      return true;
    }
  }
  return false;
}

void collectOps(const Stmt *s, std::vector<const Stmt *> &out) {
  if (!s) {
    return;
  }
  for (const Stmt *ch : s->children()) {
    collectOps(ch, out);
  }
  if (llvm::isa<CallExpr>(s) || llvm::isa<CXXNewExpr>(s) || llvm::isa<CXXConstructExpr>(s)) {
    out.push_back(s);
  }
}

class Collector : public clang::RecursiveASTVisitor<Collector> {
public:
  Collector(std::map<std::string, Node> &nodes,
            std::map<std::string, std::set<std::string>> &heapExtra,
            const SourceManager &sm,
            clang::ASTContext &ctx,
            Reporter &reporter)
      : nodes(nodes), heapExtra(heapExtra), sm(sm), ctx(ctx), reporter(reporter) {}

  // CXXMethodDecl and the constructor family do not go through TraverseFunctionDecl.
  template <typename DeclT, typename Walk>
  bool enter(DeclT *d, Walk &&walk) {
    const FunctionDecl *prev = current;
    const bool mine = d && d->isThisDeclarationADefinition() && !d->isImplicit();
    if (mine) {
      current = d->getCanonicalDecl();
      note(current);
    }
    const bool ok = walk();
    if (mine && current && current->getBody()) {
      markHeld(current);
    }
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

  bool TraverseVarDecl(clang::VarDecl *vd) {
    const bool gate =
        vd && vd->hasGlobalStorage() && !vd->isStaticLocal() && vd->isThisDeclarationADefinition();
    if (gate) {
      ++staticDepth;
    }
    const bool ok = RecursiveASTVisitor::TraverseVarDecl(vd);
    if (gate) {
      --staticDepth;
    }
    return ok;
  }

  bool VisitCallExpr(CallExpr *call) {
    if (!call) {
      return true;
    }
    const FunctionDecl *callee = call->getDirectCallee();
    const std::string to = remember(callee);
    if (to.empty()) {
      return true;
    }
    if (current && heapKind(callee) != HeapKind::No) {
      recordHeap(call->getBeginLoc());
    }
    if (!current) {
      if (staticDepth > 0) {
        heapExtra[kStaticInit].insert(to);
      }
      return true;
    }
    addEdge(keyOf(current, sm), to, siteOf(call->getBeginLoc()));
    return true;
  }

  bool VisitCXXNewExpr(CXXNewExpr *e) {
    if (!e) {
      return true;
    }
    std::string to;
    if (const FunctionDecl *op = e->getOperatorNew()) {
      to = remember(op);
    }
    if (to.empty()) {
      to = syntheticNew();
    }
    if (current) {
      recordHeap(e->getBeginLoc());
      addEdge(keyOf(current, sm), to, siteOf(e->getBeginLoc()));
    } else if (staticDepth > 0) {
      heapExtra[kStaticInit].insert(to);
    }
    return true;
  }

  bool VisitCXXDeleteExpr(CXXDeleteExpr *e) {
    if (!e || !current) {
      return true;
    }
    if (const FunctionDecl *op = e->getOperatorDelete()) {
      remember(op);
    }
    recordHeap(e->getBeginLoc());
    return true;
  }

  bool VisitCXXConstructExpr(CXXConstructExpr *e) {
    if (!e) {
      return true;
    }
    const std::string to = remember(e->getConstructor());
    if (to.empty()) {
      return true;
    }
    if (!current) {
      if (staticDepth > 0) {
        heapExtra[kStaticInit].insert(to);
      }
      return true;
    }
    addEdge(keyOf(current, sm), to, siteOf(e->getBeginLoc()));
    return true;
  }

private:
  void note(const FunctionDecl *fn) {
    const std::string key = keyOf(fn, sm);
    if (key.empty()) {
      return;
    }
    Node &node = nodes[key];
    if (node.name.empty()) {
      node.name = nameOf(fn);
    }
    if (isIsr(fn)) {
      node.isr = true;
    }
    if (isLogOrConsole(fn)) {
      node.log = true;
    }
    if (isBlocker(fn)) {
      node.block = true;
    }
  }

  std::string remember(const FunctionDecl *fn) {
    if (!fn || fn->isImplicit()) {
      return {};
    }
    note(fn);
    return keyOf(fn, sm);
  }

  std::string syntheticNew() {
    Node &node = nodes[kOperatorNew];
    node.block = true;
    if (node.name.empty()) {
      node.name = "operator new";
    }
    return kOperatorNew;
  }

  DiagSite siteOf(SourceLocation loc) const {
    if (const std::optional<DiagSite> site = reporter.locate(sm, loc)) {
      return *site;
    }
    return {};
  }

  void addEdge(const std::string &from, const std::string &to, const DiagSite &at) {
    if (from.empty() || to.empty()) {
      return;
    }
    const CallKey key{to, at};
    std::map<CallKey, bool> &outs = nodes[from].out;
    if (outs.find(key) == outs.end()) {
      outs.emplace(key, false);
    }
  }

  void recordHeap(SourceLocation loc) {
    if (!current) {
      return;
    }
    const std::string id = keyOf(current, sm);
    if (id.empty()) {
      return;
    }
    const std::optional<DiagSite> site = reporter.locate(sm, loc);
    if (!site) {
      return;
    }
    nodes[id].heap.push_back(*site);
  }

  std::string targetOf(const Stmt *s) {
    if (const auto *call = llvm::dyn_cast<CallExpr>(s)) {
      return remember(call->getDirectCallee());
    }
    if (const auto *nw = llvm::dyn_cast<CXXNewExpr>(s)) {
      if (const FunctionDecl *op = nw->getOperatorNew()) {
        const std::string to = remember(op);
        if (!to.empty()) {
          return to;
        }
      }
      return syntheticNew();
    }
    if (const auto *ctor = llvm::dyn_cast<CXXConstructExpr>(s)) {
      return remember(ctor->getConstructor());
    }
    return {};
  }

  void markHeldEdge(const std::string &from, const std::string &to, SourceLocation loc) {
    if (from.empty() || to.empty()) {
      return;
    }
    const std::optional<DiagSite> site = reporter.locate(sm, loc);
    if (!site) {
      return;
    }
    const CallKey key{to, *site};
    const auto it = nodes[from].out.find(key);
    if (it != nodes[from].out.end()) {
      it->second = true;
    }
  }

  void markHeld(const FunctionDecl *fn) {
    const Stmt *body = fn->getBody();
    if (!body || !touchesMask(body)) {
      return;
    }
    const FunctionDecl *def = fn->getDefinition();
    if (!def) {
      def = fn;
    }
    Stmt *cfgBody = def->getBody();
    if (!cfgBody) {
      return;
    }
    std::unique_ptr<CFG> cfg = CFG::buildCFG(def, cfgBody, &ctx, CFG::BuildOptions());
    if (!cfg) {
      return;
    }
    const std::string from = keyOf(fn, sm);
    const unsigned n = cfg->getNumBlockIDs();
    std::vector<char> seen(n, 0);
    std::vector<MaskDepth> in_depth(n);
    std::vector<const CFGBlock *> work;
    const CFGBlock &entry = cfg->getEntry();
    seen[entry.getBlockID()] = 1;
    work.push_back(&entry);
    const CFGBlock *exit = &cfg->getExit();

    auto apply = [&](MaskDepth d, const CFGBlock &b) {
      for (const auto &el : b) {
        if (std::optional<CFGStmt> cs = el.getAs<CFGStmt>()) {
          std::vector<const Stmt *> ops;
          collectOps(cs->getStmt(), ops);
          for (const Stmt *op : ops) {
            const std::string to = targetOf(op);
            if (d.held()) {
              markHeldEdge(from, to, op->getBeginLoc());
            }
            if (const auto *call = llvm::dyn_cast<CallExpr>(op)) {
              adjustMask(d, call->getDirectCallee());
            }
          }
        }
      }
      return d;
    };

    while (!work.empty()) {
      const CFGBlock *b = work.back();
      work.pop_back();
      const MaskDepth d = apply(in_depth[b->getBlockID()], *b);
      if (b->hasNoReturnElement()) {
        continue;
      }
      for (const CFGBlock *succ : b->succs()) {
        if (!succ || succ == exit) {
          continue;
        }
        const unsigned id = succ->getBlockID();
        if (!seen[id]) {
          seen[id] = 1;
          in_depth[id] = d;
          work.push_back(succ);
        }
      }
    }
  }

  std::map<std::string, Node> &nodes;
  std::map<std::string, std::set<std::string>> &heapExtra;
  const SourceManager &sm;
  clang::ASTContext &ctx;
  Reporter &reporter;
  const FunctionDecl *current = nullptr;
  int staticDepth = 0;
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
    for (const auto &edge : it->second.out) {
      const Color seen = color[edge.first.to];
      if (seen == Gray) {
        reporter.emitSite(edge.first.at, kRecurse, "do not recurse");
      } else if (seen == White) {
        self(self, edge.first.to);
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

bool reaches(const std::map<std::string, Node> &nodes,
             std::map<std::string, Reach> &memo,
             const std::string &id,
             bool (*pred)(const Node &)) {
  const auto it = nodes.find(id);
  if (it != nodes.end() && pred(it->second)) {
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
    for (const auto &edge : it->second.out) {
      if (reaches(nodes, memo, edge.first.to, pred)) {
        hit = true;
        break;
      }
    }
  }
  memo[id] = hit ? Yes : No;
  return hit;
}

bool nodeLogs(const Node &n) {
  return n.log;
}
bool nodeBlocks(const Node &n) {
  return n.block;
}

void reportIsr(const std::map<std::string, Node> &nodes, Reporter &reporter) {
  std::map<std::string, Reach> memo;
  for (const auto &kv : nodes) {
    for (const auto &edge : kv.second.out) {
      const auto target = nodes.find(edge.first.to);
      if (target != nodes.end() && target->second.isr) {
        reporter.emitSite(edge.first.at, kIsrCall, "do not call an ISR like a normal function");
      }
    }
    if (!kv.second.isr) {
      continue;
    }
    for (const auto &edge : kv.second.out) {
      if (reaches(nodes, memo, edge.first.to, nodeLogs)) {
        reporter.emitSite(
            edge.first.at, kIsrLog, "do not format logs or call the console from an ISR");
      }
    }
  }
}

void reportBlock(const std::map<std::string, Node> &nodes, Reporter &reporter) {
  std::map<std::string, Reach> memo;
  for (const auto &kv : nodes) {
    for (const auto &edge : kv.second.out) {
      if (!edge.second) {
        continue;
      }
      if (!reaches(nodes, memo, edge.first.to, nodeBlocks)) {
        continue;
      }
      const auto target = nodes.find(edge.first.to);
      const char *name = target == nodes.end() ? "" : target->second.name.c_str();
      if (reporter.allows(kBlock, name)) {
        continue;
      }
      reporter.emitSite(edge.first.at, kBlock, kBlockMsg);
    }
  }
}

void linkOuts(const std::map<std::string, Node> &nodes,
              const std::map<std::string, std::set<std::string>> &heapExtra,
              std::map<std::string, std::vector<std::string>> &outs,
              std::map<std::string, int> &incoming) {
  auto link = [&](const std::string &from, const std::string &to) {
    if (from.empty() || to.empty()) {
      return;
    }
    outs[from].push_back(to);
    incoming.emplace(to, 0);
    ++incoming[to];
  };
  for (const auto &kv : nodes) {
    incoming.emplace(kv.first, 0);
    for (const auto &edge : kv.second.out) {
      link(kv.first, edge.first.to);
    }
  }
  for (const auto &kv : heapExtra) {
    incoming.emplace(kv.first, 0);
    for (const std::string &to : kv.second) {
      link(kv.first, to);
    }
  }
}

std::set<std::string> forward(const std::map<std::string, std::vector<std::string>> &outs,
                              const std::vector<std::string> &seeds) {
  std::set<std::string> seen;
  std::vector<std::string> work;
  for (const std::string &id : seeds) {
    if (seen.insert(id).second) {
      work.push_back(id);
    }
  }
  while (!work.empty()) {
    const std::string id = work.back();
    work.pop_back();
    const auto it = outs.find(id);
    if (it == outs.end()) {
      continue;
    }
    for (const std::string &to : it->second) {
      if (seen.insert(to).second) {
        work.push_back(to);
      }
    }
  }
  return seen;
}

void reportHeap(const std::map<std::string, Node> &nodes,
                const std::map<std::string, std::set<std::string>> &heapExtra,
                Reporter &reporter) {
  // An allocation is init-phase when every path to it starts in static
  // initialization or in a function named by --allow. A callee in another
  // file is included. A path from any other entry still reports.
  std::map<std::string, std::vector<std::string>> outs;
  std::map<std::string, int> incoming;
  linkOuts(nodes, heapExtra, outs, incoming);

  std::vector<std::string> initSeeds;
  std::vector<std::string> runtimeSeeds;
  const auto extra = heapExtra.find(kStaticInit);
  if (extra != heapExtra.end() && !extra->second.empty()) {
    initSeeds.push_back(kStaticInit);
  }
  for (const auto &kv : nodes) {
    if (reporter.allows(kHeap, kv.second.name.c_str())) {
      initSeeds.push_back(kv.first);
      continue;
    }
    if (incoming[kv.first] == 0) {
      runtimeSeeds.push_back(kv.first);
    }
  }
  const std::set<std::string> init = forward(outs, initSeeds);
  const std::set<std::string> runtime = forward(outs, runtimeSeeds);
  for (const auto &kv : nodes) {
    if (kv.second.heap.empty()) {
      continue;
    }
    if (init.count(kv.first) != 0 && runtime.count(kv.first) == 0) {
      continue;
    }
    for (const DiagSite &site : kv.second.heap) {
      reporter.emitSite(site, kHeap, kHeapMsg);
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
  // Callees of file-scope initializers. Kept off the call graph so a global
  // constructor is not also a recursion edge.
  std::map<std::string, std::set<std::string>> heapExtra;
};

ProgramCallGraph::ProgramCallGraph() : data(std::make_unique<Data>()) {}

ProgramCallGraph::~ProgramCallGraph() = default;

void ProgramCallGraph::record(TranslationUnitDecl *tu,
                              const SourceManager &sm,
                              Reporter &reporter) {
  if (!tu) {
    return;
  }
  Collector collector(data->nodes, data->heapExtra, sm, tu->getASTContext(), reporter);
  collector.TraverseDecl(tu);
}

void ProgramCallGraph::finish(Reporter &reporter) const {
  reportRecursion(data->nodes, reporter);
  reportIsr(data->nodes, reporter);
  reportBlock(data->nodes, reporter);
  reportHeap(data->nodes, data->heapExtra, reporter);
}

void attachCallGraph(clang::ast_matchers::MatchFinder &finder,
                     ProgramCallGraph &graph,
                     Reporter &reporter,
                     std::unique_ptr<clang::ast_matchers::MatchFinder::MatchCallback> &slot) {
  slot = std::make_unique<Pass>(graph, reporter);
  finder.addMatcher(translationUnitDecl().bind("tu"), slot.get());
}
