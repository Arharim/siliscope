#include "siliscope/Interproc.h"

#include "siliscope/FunctionKey.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMap.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

using clang::ArraySubscriptExpr;
using clang::ASTContext;
using clang::BinaryOperator;
using clang::CallExpr;
using clang::CFG;
using clang::CFGBlock;
using clang::CFGStmt;
using clang::ConditionalOperator;
using clang::CXXOperatorCallExpr;
using clang::Decl;
using clang::DeclRefExpr;
using clang::DeclStmt;
using clang::DoStmt;
using clang::Expr;
using clang::ForStmt;
using clang::FunctionDecl;
using clang::IfStmt;
using clang::IntegerLiteral;
using clang::ParmVarDecl;
using clang::QualType;
using clang::ReturnStmt;
using clang::SourceManager;
using clang::Stmt;
using clang::UnaryOperator;
using clang::VarDecl;
using clang::WhileStmt;

namespace {

constexpr const char *kUninit = "ss.expr.uninit";
constexpr const char *kBounds = "ss.mem.bounds";
constexpr const char *kNull = "ss.mem.no-null-deref";
constexpr const char *kOnePast = "ss.ptr.no-deref-one-past";

constexpr const char *kUninitMsg = "read of an uninitialized object";
constexpr const char *kBoundsMsg = "index is outside the array";
constexpr const char *kNullMsg = "dereference of a null pointer";
constexpr const char *kOnePastMsg = "dereference of a one-past-the-end pointer";

bool seenPair(const std::vector<std::pair<std::string, unsigned>> &seen,
              const std::string &key,
              unsigned arg) {
  for (const auto &item : seen) {
    if (item.first == key && item.second == arg) {
      return true;
    }
  }
  return false;
}

bool seenKey(const std::vector<std::string> &seen, const std::string &key) {
  for (const std::string &item : seen) {
    if (item == key) {
      return true;
    }
  }
  return false;
}

const CallArg *argAt(const CallNote &note, unsigned index) {
  for (const CallArg &arg : note.args) {
    if (arg.index == index) {
      return &arg;
    }
  }
  return nullptr;
}

bool outOfBounds(int64_t off, int64_t idx, uint64_t size) {
  if (off < 0 || idx < 0) {
    return true;
  }
  const auto a = static_cast<uint64_t>(off);
  const auto b = static_cast<uint64_t>(idx);
  if (a > std::numeric_limits<uint64_t>::max() - b) {
    return true;
  }
  return a + b >= size;
}

bool onePastEnd(int64_t off, int64_t idx, uint64_t size) {
  if (off < 0 || idx < 0) {
    return false;
  }
  const auto a = static_cast<uint64_t>(off);
  const auto b = static_cast<uint64_t>(idx);
  if (a > std::numeric_limits<uint64_t>::max() - b) {
    return false;
  }
  return a + b == size;
}

} // namespace

uint32_t ProgramFacts::nextId() {
  return ++ids;
}

void ProgramFacts::putSummary(std::string key, Summary summary) {
  if (key.empty()) {
    return;
  }
  summaries.emplace(std::move(key), std::move(summary));
}

const Summary *ProgramFacts::find(const std::string &key) const {
  const auto it = summaries.find(key);
  if (it == summaries.end()) {
    return nullptr;
  }
  return &it->second;
}

ProgramFacts::ParamEffect ProgramFacts::effectOf(const std::string &key, unsigned arg) const {
  std::vector<std::pair<std::string, unsigned>> seen;
  switch (resolveParam(key, arg, 0, seen).kind) {
  case Touch::Kind::Missing:
    return ParamEffect::Missing;
  case Touch::Kind::Read:
    return ParamEffect::Read;
  case Touch::Kind::Write:
    return ParamEffect::Write;
  case Touch::Kind::Untouched:
    return ParamEffect::Untouched;
  case Touch::Kind::Opaque:
    return ParamEffect::Opaque;
  }
  return ParamEffect::Opaque;
}

void ProgramFacts::noteCall(uint32_t id, CallNote note) {
  if (id == 0 || note.callee.empty()) {
    return;
  }
  if (calls.size() <= id) {
    calls.resize(id + 1);
  }
  if (!calls[id].callee.empty()) {
    return;
  }
  calls[id] = std::move(note);
}

void ProgramFacts::noteRetUse(RetUse use) {
  if (use.call == 0) {
    return;
  }
  uses.push_back(std::move(use));
}

void ProgramFacts::noteLater(LaterRead use) {
  if (use.deps.empty()) {
    return;
  }
  laters.push_back(std::move(use));
}

ProgramFacts::Touch ProgramFacts::resolveParam(
    const std::string &key,
    unsigned arg,
    int depth,
    std::vector<std::pair<std::string, unsigned>> &seen) const {
  Touch miss;
  if (key.empty() || depth > 12 || seenPair(seen, key, arg)) {
    miss.kind = Touch::Kind::Opaque;
    return miss;
  }
  const auto it = summaries.find(key);
  if (it == summaries.end() || arg >= it->second.params.size()) {
    return miss;
  }
  seen.emplace_back(key, arg);
  const Summary::Param &p = it->second.params[arg];
  const bool pureForward =
      p.forward && !p.poisoned && !p.readFirst && !p.writeFirst && !p.deref && p.idxs.empty();
  if (pureForward) {
    return resolveParam(p.fwdKey, p.fwdArg, depth + 1, seen);
  }
  Touch out;
  out.deref = p.deref;
  out.idxs = p.idxs;
  if (p.readFirst) {
    out.kind = Touch::Kind::Read;
  } else if (p.writeFirst) {
    out.kind = Touch::Kind::Write;
  } else if (p.clean && !p.poisoned && !p.forward) {
    out.kind = Touch::Kind::Untouched;
  } else {
    out.kind = Touch::Kind::Opaque;
  }
  return out;
}

ProgramFacts::Returned ProgramFacts::resolveRet(const std::string &key,
                                                int depth,
                                                std::vector<std::string> &seen) const {
  Returned none;
  if (key.empty() || depth > 12 || seenKey(seen, key)) {
    return none;
  }
  const auto it = summaries.find(key);
  if (it == summaries.end()) {
    return none;
  }
  seen.push_back(key);
  const Summary::Ret &r = it->second.ret;
  if (r.kind == Summary::Ret::Kind::Null) {
    none.kind = Returned::Kind::Null;
    return none;
  }
  if (r.kind == Summary::Ret::Kind::Param) {
    none.kind = Returned::Kind::Param;
    none.param = r.param;
    none.off = r.off;
    return none;
  }
  if (r.kind == Summary::Ret::Kind::Forward) {
    Returned inner = resolveRet(r.fwdKey, depth + 1, seen);
    if (inner.kind == Returned::Kind::Param) {
      if (inner.param >= r.fwdMap.size()) {
        return {};
      }
      const int mapped = r.fwdMap[inner.param];
      if (mapped < 0) {
        return {};
      }
      inner.param = static_cast<unsigned>(mapped);
      if (inner.off > std::numeric_limits<int64_t>::max() - r.off) {
        return {};
      }
      inner.off += r.off;
    }
    return inner;
  }
  return none;
}

void ProgramFacts::emitAt(Reporter &reporter,
                          const DiagSite &at,
                          const std::string &caller,
                          const std::string &name,
                          const char *id,
                          const char *msg) const {
  if (!caller.empty() && reporter.allows(id, caller.c_str())) {
    return;
  }
  if (!name.empty() && reporter.allows(id, name.c_str())) {
    return;
  }
  reporter.emitSite(at, id, msg);
}

void ProgramFacts::finish(Reporter &reporter) const {
  for (const CallNote &note : calls) {
    if (note.callee.empty()) {
      continue;
    }
    for (const CallArg &arg : note.args) {
      std::vector<std::pair<std::string, unsigned>> seen;
      const Touch touch = resolveParam(note.callee, arg.index, 0, seen);
      if (arg.nullMust && touch.deref) {
        emitAt(reporter, note.at, note.caller, arg.name, kNull, kNullMsg);
      }
      if (arg.uninit && touch.kind == Touch::Kind::Read) {
        emitAt(reporter, note.at, note.caller, arg.name, kUninit, kUninitMsg);
      }
      if (arg.bound) {
        for (const int64_t idx : touch.idxs) {
          if (onePastEnd(arg.off, idx, arg.size)) {
            emitAt(reporter, note.at, note.caller, arg.name, kOnePast, kOnePastMsg);
          }
          if (outOfBounds(arg.off, idx, arg.size)) {
            emitAt(reporter, note.at, note.caller, arg.name, kBounds, kBoundsMsg);
          }
        }
      }
    }
  }
  for (const RetUse &use : uses) {
    if (use.call >= calls.size() || calls[use.call].callee.empty()) {
      continue;
    }
    const CallNote &note = calls[use.call];
    std::vector<std::string> seen;
    const Returned ret = resolveRet(note.callee, 0, seen);
    const CallArg *arg = ret.kind == Returned::Kind::Param ? argAt(note, ret.param) : nullptr;
    const bool nullRet = ret.kind == Returned::Kind::Null || (arg && arg->nullMust);
    if (nullRet) {
      emitAt(reporter, use.at, use.caller, use.name, kNull, kNullMsg);
    }
    if (arg && arg->bound) {
      if (ret.off > std::numeric_limits<int64_t>::max() - arg->off) {
        continue;
      }
      const int64_t off = arg->off + ret.off + use.off;
      const int64_t idx = use.kind == RetUse::Kind::Deref ? 0 : use.index;
      if (onePastEnd(off, idx, arg->size)) {
        emitAt(reporter, use.at, use.caller, use.name, kOnePast, kOnePastMsg);
      }
      if (outOfBounds(off, idx, arg->size)) {
        emitAt(reporter, use.at, use.caller, use.name, kBounds, kBoundsMsg);
      }
    }
  }
  for (const LaterRead &later : laters) {
    bool untouched = !later.deps.empty();
    for (const auto &dep : later.deps) {
      if (dep.first >= calls.size() || calls[dep.first].callee.empty()) {
        untouched = false;
        break;
      }
      std::vector<std::pair<std::string, unsigned>> seen;
      const Touch touch = resolveParam(calls[dep.first].callee, dep.second, 0, seen);
      if (touch.kind != Touch::Kind::Untouched) {
        untouched = false;
        break;
      }
    }
    if (untouched) {
      emitAt(reporter, later.at, later.caller, later.name, kUninit, kUninitMsg);
    }
  }
}

namespace {

struct Alias {
  unsigned param = 0;
  int64_t off = 0;
  bool ref = false;
  bool operator==(const Alias &o) const { return param == o.param && off == o.off && ref == o.ref; }
};

struct PBit {
  bool deref = false;
  bool readFirst = false;
  bool writeFirst = false;
  bool clean = true;
  bool poisoned = false;
  bool forward = false;
  bool idxsValid = true;
  std::vector<int64_t> idxs;
  std::string fwdKey;
  unsigned fwdArg = 0;
};

struct BState {
  llvm::DenseMap<const VarDecl *, Alias> alias;
  std::vector<char> nonNull;
  std::vector<PBit> bits;
  Summary::Ret ret;
};

struct Val {
  bool ok = true;
  bool isNull = false;
  bool isForward = false;
  std::optional<Alias> ptr;
  std::string fwdKey;
  std::vector<int> fwdMap;
};

bool sameIdx(const std::vector<int64_t> &a, const std::vector<int64_t> &b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (const int64_t v : a) {
    bool found = false;
    for (const int64_t w : b) {
      if (v == w) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  return true;
}

bool sameBit(const PBit &a, const PBit &b) {
  return a.deref == b.deref && a.readFirst == b.readFirst && a.writeFirst == b.writeFirst &&
         a.clean == b.clean && a.poisoned == b.poisoned && a.forward == b.forward &&
         a.idxsValid == b.idxsValid && a.fwdKey == b.fwdKey && a.fwdArg == b.fwdArg &&
         sameIdx(a.idxs, b.idxs);
}

bool sameRet(const Summary::Ret &a, const Summary::Ret &b) {
  return a.kind == b.kind && a.param == b.param && a.off == b.off && a.fwdKey == b.fwdKey &&
         a.fwdMap == b.fwdMap;
}

bool sameState(const BState &a, const BState &b) {
  if (a.alias.size() != b.alias.size() || a.nonNull != b.nonNull ||
      a.bits.size() != b.bits.size() || !sameRet(a.ret, b.ret)) {
    return false;
  }
  for (const auto &kv : a.alias) {
    const auto it = b.alias.find(kv.first);
    if (it == b.alias.end() || !(it->second == kv.second)) {
      return false;
    }
  }
  for (size_t i = 0; i < a.bits.size(); ++i) {
    if (!sameBit(a.bits[i], b.bits[i])) {
      return false;
    }
  }
  return true;
}

std::vector<int64_t> intersectIdx(const std::vector<int64_t> &a, const std::vector<int64_t> &b) {
  std::vector<int64_t> out;
  for (const int64_t v : a) {
    for (const int64_t w : b) {
      if (v == w) {
        out.push_back(v);
        break;
      }
    }
  }
  return out;
}

class Summarizer {
public:
  Summarizer(const FunctionDecl &fn, const CFG &cfg, ASTContext &ctx, const SourceManager &sm)
      : fn(fn), cfg(cfg), ctx(ctx), sm(sm) {}

  Summary run() {
    const unsigned n = fn.getNumParams();
    initial.bits.assign(n, PBit{});
    initial.nonNull.assign(n, 0);
    for (unsigned i = 0; i < n; ++i) {
      const ParmVarDecl *p = fn.getParamDecl(i);
      if (!p || p->getType().isNull() || !p->getType()->isPointerType()) {
        // Only a pointer is summarized. Anything else stays unknown so a
        // reference write is not treated as leaving the object untouched.
        initial.bits[i].clean = false;
        initial.bits[i].poisoned = true;
        continue;
      }
      Alias a;
      a.param = i;
      initial.alias[p->getCanonicalDecl()] = a;
    }
    if (fn.getBody()) {
      parents = std::make_unique<clang::ParentMap>(fn.getBody());
    }
    const unsigned blocks = cfg.getNumBlockIDs();
    outs.clear();
    outs.resize(blocks);
    ready.assign(blocks, 0);
    queued.assign(blocks, 0);
    std::vector<const CFGBlock *> work;
    auto enqueue = [&](const CFGBlock *b) {
      if (!b) {
        return;
      }
      const unsigned id = b->getBlockID();
      if (queued[id]) {
        return;
      }
      queued[id] = 1;
      work.push_back(b);
    };
    enqueue(&cfg.getEntry());
    unsigned steps = 0;
    const unsigned limit = blocks * 48 + 8;
    bool gaveUp = false;
    while (!work.empty()) {
      if (++steps > limit) {
        gaveUp = true;
        break;
      }
      const CFGBlock *b = work.back();
      work.pop_back();
      const unsigned id = b->getBlockID();
      queued[id] = 0;
      BState next = incoming(b);
      cur = &next;
      transfer(b);
      cur = nullptr;
      if (!ready[id] || !sameState(next, outs[id])) {
        outs[id] = std::move(next);
        ready[id] = 1;
        for (const CFGBlock *succ : b->succs()) {
          enqueue(succ);
        }
      }
    }
    Summary out;
    out.params.resize(n);
    if (gaveUp || !ready[cfg.getExit().getBlockID()]) {
      for (Summary::Param &p : out.params) {
        p.clean = false;
        p.poisoned = true;
      }
      out.ret.kind = Summary::Ret::Kind::Wild;
      return out;
    }
    const BState &exit = outs[cfg.getExit().getBlockID()];
    for (unsigned i = 0; i < n; ++i) {
      const PBit &b = exit.bits[i];
      Summary::Param &p = out.params[i];
      p.deref = b.deref;
      p.readFirst = b.readFirst;
      p.writeFirst = b.writeFirst;
      p.poisoned = b.poisoned;
      p.clean = b.clean && !b.poisoned && !b.forward && !b.readFirst && !b.writeFirst && !b.deref;
      p.forward =
          b.forward && !b.poisoned && !b.readFirst && !b.writeFirst && !b.deref && b.idxs.empty();
      p.idxs = b.idxsValid ? b.idxs : std::vector<int64_t>{};
      p.fwdKey = b.fwdKey;
      p.fwdArg = b.fwdArg;
    }
    out.ret = exit.ret;
    if (out.ret.kind == Summary::Ret::Kind::Forward && out.ret.fwdKey.empty()) {
      out.ret.kind = Summary::Ret::Kind::Wild;
    }
    return out;
  }

private:
  const FunctionDecl &fn;
  const CFG &cfg;
  ASTContext &ctx;
  const SourceManager &sm;
  BState initial;
  BState *cur = nullptr;
  std::vector<BState> outs;
  std::vector<char> ready;
  std::vector<char> queued;
  std::unique_ptr<clang::ParentMap> parents;

  BState join(const std::vector<BState> &preds) const {
    BState out = preds[0];
    for (size_t pi = 1; pi < preds.size(); ++pi) {
      const BState &p = preds[pi];
      llvm::DenseMap<const VarDecl *, Alias> kept;
      for (const auto &kv : out.alias) {
        const auto it = p.alias.find(kv.first);
        if (it != p.alias.end() && it->second == kv.second) {
          kept[kv.first] = kv.second;
        }
      }
      out.alias = std::move(kept);
      for (size_t i = 0; i < out.nonNull.size() && i < p.nonNull.size(); ++i) {
        out.nonNull[i] = static_cast<char>(out.nonNull[i] && p.nonNull[i]);
      }
      for (size_t i = 0; i < out.bits.size() && i < p.bits.size(); ++i) {
        PBit &a = out.bits[i];
        const PBit &b = p.bits[i];
        a.deref = a.deref && b.deref;
        a.readFirst = a.readFirst && b.readFirst;
        a.writeFirst = a.writeFirst && b.writeFirst;
        a.clean = a.clean && b.clean;
        a.poisoned = a.poisoned || b.poisoned;
        if (a.idxsValid && b.idxsValid) {
          a.idxs = intersectIdx(a.idxs, b.idxs);
        } else {
          a.idxs.clear();
          a.idxsValid = true;
        }
        if (!(a.forward && b.forward && a.fwdKey == b.fwdKey && a.fwdArg == b.fwdArg)) {
          a.forward = false;
          a.fwdKey.clear();
          a.fwdArg = 0;
        }
      }
      if (!sameRet(out.ret, p.ret)) {
        if (out.ret.kind == Summary::Ret::Kind::None && p.ret.kind == Summary::Ret::Kind::None) {
          continue;
        }
        out.ret = Summary::Ret{};
        out.ret.kind = Summary::Ret::Kind::Wild;
      }
    }
    for (PBit &b : out.bits) {
      if (b.poisoned) {
        b.clean = false;
        b.forward = false;
      }
    }
    return out;
  }

  int edgeIndex(const CFGBlock *from, const CFGBlock *to) const {
    const Stmt *term = from->getTerminatorStmt();
    if (!llvm::isa_and_nonnull<IfStmt>(term) && !llvm::isa_and_nonnull<WhileStmt>(term) &&
        !llvm::isa_and_nonnull<DoStmt>(term) && !llvm::isa_and_nonnull<ForStmt>(term) &&
        !llvm::isa_and_nonnull<ConditionalOperator>(term)) {
      return -1;
    }
    int index = -1;
    int matches = 0;
    int at = 0;
    for (const CFGBlock *succ : from->succs()) {
      if (succ == to) {
        index = at;
        ++matches;
      }
      ++at;
    }
    if (matches != 1 || (index != 0 && index != 1)) {
      return -1;
    }
    return index;
  }

  const Expr *termCond(const Stmt *term) const {
    if (!term) {
      return nullptr;
    }
    if (const auto *i = llvm::dyn_cast<IfStmt>(term)) {
      return i->getCond();
    }
    if (const auto *w = llvm::dyn_cast<WhileStmt>(term)) {
      return w->getCond();
    }
    if (const auto *d = llvm::dyn_cast<DoStmt>(term)) {
      return d->getCond();
    }
    if (const auto *f = llvm::dyn_cast<ForStmt>(term)) {
      return f->getCond();
    }
    if (const auto *c = llvm::dyn_cast<ConditionalOperator>(term)) {
      return c->getCond();
    }
    return nullptr;
  }

  const VarDecl *asVar(const Expr *e) const {
    e = e ? e->IgnoreParenImpCasts() : nullptr;
    const auto *dr = llvm::dyn_cast_or_null<DeclRefExpr>(e);
    if (!dr) {
      return nullptr;
    }
    const auto *vd = llvm::dyn_cast<VarDecl>(dr->getDecl());
    return vd ? vd->getCanonicalDecl() : nullptr;
  }

  void refine(BState &s, const Expr *cond, bool whenTrue) const {
    if (!cond) {
      return;
    }
    cond = cond->IgnoreParenImpCasts();
    if (const auto *uo = llvm::dyn_cast<UnaryOperator>(cond)) {
      if (uo->getOpcode() == clang::UO_LNot) {
        refine(s, uo->getSubExpr(), !whenTrue);
      }
      return;
    }
    if (const auto *bo = llvm::dyn_cast<BinaryOperator>(cond)) {
      const auto op = bo->getOpcode();
      if (op == clang::BO_LAnd && whenTrue) {
        refine(s, bo->getLHS(), true);
        refine(s, bo->getRHS(), true);
        return;
      }
      if (op == clang::BO_LOr && !whenTrue) {
        refine(s, bo->getLHS(), false);
        refine(s, bo->getRHS(), false);
        return;
      }
      if (op == clang::BO_EQ || op == clang::BO_NE) {
        const bool equal = (op == clang::BO_EQ) == whenTrue;
        const Expr *varSide = nullptr;
        const Expr *other = nullptr;
        if (asVar(bo->getLHS())) {
          varSide = bo->getLHS();
          other = bo->getRHS();
        } else if (asVar(bo->getRHS())) {
          varSide = bo->getRHS();
          other = bo->getLHS();
        }
        if (!varSide || !isNullPointer(other)) {
          return;
        }
        if (const VarDecl *vd = asVar(varSide)) {
          const auto it = s.alias.find(vd);
          if (it != s.alias.end() && !it->second.ref && !equal &&
              it->second.param < s.nonNull.size()) {
            s.nonNull[it->second.param] = 1;
          }
        }
        return;
      }
    }
    if (const VarDecl *vd = asVar(cond)) {
      const auto it = s.alias.find(vd);
      if (it != s.alias.end() && !it->second.ref && whenTrue &&
          it->second.param < s.nonNull.size()) {
        s.nonNull[it->second.param] = 1;
      }
    }
  }

  BState incoming(const CFGBlock *b) const {
    if (b == &cfg.getEntry()) {
      return initial;
    }
    std::vector<BState> preds;
    for (const CFGBlock *p : b->preds()) {
      if (!p || !ready[p->getBlockID()]) {
        continue;
      }
      BState s = outs[p->getBlockID()];
      const int edge = edgeIndex(p, b);
      if (edge >= 0) {
        refine(s, termCond(p->getTerminatorStmt()), edge == 0);
      }
      preds.push_back(std::move(s));
    }
    if (preds.empty()) {
      return initial;
    }
    return join(preds);
  }

  void poison(unsigned i) {
    if (!cur || i >= cur->bits.size()) {
      return;
    }
    PBit &b = cur->bits[i];
    b.poisoned = true;
    b.clean = false;
    b.forward = false;
    b.fwdKey.clear();
  }

  void addIndex(PBit &b, int64_t idx) {
    if (!b.idxsValid) {
      return;
    }
    for (const int64_t have : b.idxs) {
      if (have == idx) {
        return;
      }
    }
    if (b.idxs.size() >= 4) {
      b.idxs.clear();
      b.idxsValid = false;
      return;
    }
    b.idxs.push_back(idx);
  }

  void touch(unsigned i, bool write, bool isDeref, std::optional<int64_t> idx) {
    if (!cur || i >= cur->bits.size()) {
      return;
    }
    PBit &b = cur->bits[i];
    b.forward = false;
    b.fwdKey.clear();
    b.clean = false;
    if (!b.readFirst && !b.writeFirst) {
      if (write) {
        b.writeFirst = true;
      } else {
        b.readFirst = true;
      }
    }
    if (isDeref && (i >= cur->nonNull.size() || !cur->nonNull[i])) {
      b.deref = true;
    }
    if (idx) {
      addIndex(b, *idx);
    }
  }

  void noteForward(unsigned i, const std::string &key, unsigned arg) {
    if (!cur || i >= cur->bits.size() || key.empty()) {
      return;
    }
    PBit &b = cur->bits[i];
    if (b.poisoned || b.readFirst || b.writeFirst || b.deref || !b.idxs.empty()) {
      return;
    }
    if (b.forward && (b.fwdKey != key || b.fwdArg != arg)) {
      poison(i);
      return;
    }
    b.forward = true;
    b.fwdKey = key;
    b.fwdArg = arg;
    b.clean = false;
  }

  std::optional<int64_t> constInt(const Expr *e) const {
    if (!e) {
      return std::nullopt;
    }
    e = e->IgnoreParenImpCasts();
    clang::Expr::EvalResult value;
    if (!e->EvaluateAsInt(value, ctx) || !value.Val.isInt()) {
      return std::nullopt;
    }
    const llvm::APSInt &v = value.Val.getInt();
    if (v.getSignificantBits() > 63) {
      return std::nullopt;
    }
    return v.getSExtValue();
  }

  bool isNullPointer(const Expr *e) const {
    if (!e) {
      return false;
    }
    const Expr *full = e->IgnoreParens();
    if (full->getType().isNull() || !full->getType()->isPointerType()) {
      return false;
    }
    const Expr *inner = full->IgnoreParenImpCasts();
    if (llvm::isa<clang::CXXNullPtrLiteralExpr>(inner) || llvm::isa<clang::GNUNullExpr>(inner)) {
      return true;
    }
    if (const auto *lit = llvm::dyn_cast<IntegerLiteral>(inner)) {
      return lit->getValue().isZero();
    }
    clang::Expr::EvalResult value;
    return inner->EvaluateAsInt(value, ctx) && value.Val.isInt() && value.Val.getInt().isZero();
  }

  bool mentions(const Stmt *s, const VarDecl *vd) const {
    if (!s || !vd) {
      return false;
    }
    if (const auto *dr = llvm::dyn_cast<DeclRefExpr>(s)) {
      if (const auto *v = llvm::dyn_cast<VarDecl>(dr->getDecl())) {
        if (v->getCanonicalDecl() == vd) {
          return true;
        }
      }
    }
    for (const Stmt *c : s->children()) {
      if (mentions(c, vd)) {
        return true;
      }
    }
    return false;
  }

  void poisonMentioned(const Expr *e) {
    if (!cur || !e) {
      return;
    }
    for (const auto &kv : cur->alias) {
      if (mentions(e, kv.first)) {
        poison(kv.second.param);
      }
    }
  }

  Val eval(const Expr *e, bool asWrite) {
    Val bad;
    bad.ok = false;
    if (!e || !cur) {
      return {};
    }
    if (isNullPointer(e)) {
      Val v;
      v.isNull = true;
      return v;
    }
    e = e->IgnoreParenImpCasts();
    if (!e) {
      return {};
    }
    if (llvm::isa<clang::UnaryExprOrTypeTraitExpr>(e)) {
      return {};
    }
    if (const auto *dr = llvm::dyn_cast<DeclRefExpr>(e)) {
      const auto *vd = llvm::dyn_cast<VarDecl>(dr->getDecl());
      if (!vd || !cur) {
        return {};
      }
      vd = vd->getCanonicalDecl();
      const auto it = cur->alias.find(vd);
      if (it == cur->alias.end()) {
        return {};
      }
      if (it->second.ref) {
        touch(it->second.param, asWrite, false, std::nullopt);
        return {};
      }
      if (asWrite) {
        cur->alias.erase(vd);
        return {};
      }
      Val v;
      v.ptr = it->second;
      return v;
    }
    if (const auto *uo = llvm::dyn_cast<UnaryOperator>(e)) {
      const auto op = uo->getOpcode();
      if (op == clang::UO_Deref) {
        return pointee(uo->getSubExpr(), nullptr, asWrite);
      }
      if (op == clang::UO_AddrOf) {
        if (const VarDecl *vd = asVar(uo->getSubExpr())) {
          const auto it = cur->alias.find(vd);
          if (it != cur->alias.end()) {
            poison(it->second.param);
          }
        } else {
          poisonMentioned(uo->getSubExpr());
        }
        return {};
      }
      if (uo->isIncrementDecrementOp()) {
        if (const VarDecl *vd = asVar(uo->getSubExpr())) {
          const auto it = cur->alias.find(vd);
          if (it != cur->alias.end() && !it->second.ref) {
            poison(it->second.param);
            cur->alias.erase(vd);
            return {};
          }
        }
        return eval(uo->getSubExpr(), false);
      }
      if (op == clang::UO_LNot || op == clang::UO_Plus || op == clang::UO_Minus ||
          op == clang::UO_Not) {
        Val inner = eval(uo->getSubExpr(), false);
        inner.ptr.reset();
        inner.isNull = false;
        inner.isForward = false;
        return inner;
      }
    }
    if (const auto *sub = llvm::dyn_cast<ArraySubscriptExpr>(e)) {
      return pointee(sub->getBase(), sub->getIdx(), asWrite);
    }
    if (const auto *me = llvm::dyn_cast<clang::MemberExpr>(e)) {
      if (me->isArrow()) {
        return pointee(me->getBase(), nullptr, asWrite);
      }
      return eval(me->getBase(), asWrite);
    }
    if (const auto *bo = llvm::dyn_cast<BinaryOperator>(e)) {
      return evalBin(bo, asWrite);
    }
    if (const auto *call = llvm::dyn_cast<CallExpr>(e)) {
      return evalCall(call);
    }
    if (const auto *co = llvm::dyn_cast<ConditionalOperator>(e)) {
      Val c = eval(co->getCond(), false);
      Val t = eval(co->getTrueExpr(), false);
      Val f = eval(co->getFalseExpr(), false);
      if (!c.ok || !t.ok || !f.ok) {
        return bad;
      }
      if (t.isNull && f.isNull) {
        Val v;
        v.isNull = true;
        return v;
      }
      if (t.ptr && f.ptr && *t.ptr == *f.ptr) {
        Val v;
        v.ptr = t.ptr;
        return v;
      }
      return {};
    }
    if (const auto *cast = llvm::dyn_cast<clang::CastExpr>(e)) {
      if (cast->getType()->isVoidType()) {
        return eval(cast->getSubExpr(), false);
      }
      return eval(cast->getSubExpr(), asWrite);
    }
    poisonMentioned(e);
    if (cur) {
      for (const auto &kv : cur->alias) {
        if (mentions(e, kv.first)) {
          return bad;
        }
      }
    }
    return {};
  }

  Val pointee(const Expr *base, const Expr *idx, bool asWrite) {
    Val b = eval(base, false);
    if (!b.ok) {
      return b;
    }
    std::optional<int64_t> k;
    if (idx) {
      Val iv = eval(idx, false);
      if (!iv.ok) {
        return iv;
      }
      k = constInt(idx);
    } else {
      k = 0;
    }
    if (!b.ptr) {
      return {};
    }
    const bool derefNull = b.ptr->off == 0;
    std::optional<int64_t> at = k;
    if (k && b.ptr->off > std::numeric_limits<int64_t>::max() - *k) {
      at.reset();
    } else if (k) {
      at = b.ptr->off + *k;
    }
    // A shifted pointer is not the incoming null. Still a read of the object.
    touch(b.ptr->param, asWrite, derefNull && b.ptr->off == 0, at);
    return {};
  }

  Val evalBin(const BinaryOperator *bo, bool asWrite) {
    (void)asWrite;
    const auto op = bo->getOpcode();
    if (op == clang::BO_Assign) {
      Val rhs = eval(bo->getRHS(), false);
      if (const VarDecl *vd = asVar(bo->getLHS())) {
        if (cur) {
          cur->alias.erase(vd);
          if (rhs.ok && rhs.ptr && vd->getType()->isPointerType()) {
            cur->alias[vd] = *rhs.ptr;
          }
        }
        return rhs.ok ? Val{} : rhs;
      }
      Val lhs = eval(bo->getLHS(), true);
      if (!rhs.ok || !lhs.ok) {
        Val bad;
        bad.ok = false;
        return bad;
      }
      if (rhs.ptr) {
        poison(rhs.ptr->param);
      }
      return {};
    }
    if (bo->isCompoundAssignmentOp()) {
      if (const VarDecl *vd = asVar(bo->getLHS())) {
        const auto it = cur->alias.find(vd);
        if (it != cur->alias.end() && !it->second.ref) {
          poison(it->second.param);
          cur->alias.erase(vd);
          eval(bo->getRHS(), false);
          return {};
        }
      }
      Val rhs = eval(bo->getRHS(), false);
      Val lhs = eval(bo->getLHS(), false);
      if (!rhs.ok || !lhs.ok) {
        Val bad;
        bad.ok = false;
        return bad;
      }
      return {};
    }
    if (op == clang::BO_Add || op == clang::BO_Sub) {
      Val lhs = eval(bo->getLHS(), false);
      Val rhs = eval(bo->getRHS(), false);
      if (!lhs.ok || !rhs.ok) {
        Val bad;
        bad.ok = false;
        return bad;
      }
      if (lhs.ptr && !lhs.ptr->ref) {
        if (const auto k = constInt(bo->getRHS())) {
          const int64_t delta = op == clang::BO_Sub ? -*k : *k;
          if (lhs.ptr->off > std::numeric_limits<int64_t>::max() - delta && delta > 0) {
            poison(lhs.ptr->param);
            return {};
          }
          Val v;
          v.ptr = *lhs.ptr;
          v.ptr->off += delta;
          return v;
        }
        return {};
      }
      if (rhs.ptr && !rhs.ptr->ref && op == clang::BO_Add) {
        if (const auto k = constInt(bo->getLHS())) {
          Val v;
          v.ptr = *rhs.ptr;
          if (v.ptr->off > std::numeric_limits<int64_t>::max() - *k) {
            poison(rhs.ptr->param);
            return {};
          }
          v.ptr->off += *k;
          return v;
        }
      }
      return {};
    }
    Val lhs = eval(bo->getLHS(), false);
    Val rhs = eval(bo->getRHS(), false);
    if (!lhs.ok || !rhs.ok) {
      Val bad;
      bad.ok = false;
      return bad;
    }
    return {};
  }

  Val evalCall(const CallExpr *call) {
    llvm::SmallVector<Val, 4> args;
    bool ok = true;
    for (const Expr *arg : call->arguments()) {
      args.push_back(eval(arg, false));
      if (!args.back().ok) {
        ok = false;
      }
    }
    const FunctionDecl *callee = call->getDirectCallee();
    const bool direct = callee && !llvm::isa<CXXOperatorCallExpr>(call);
    if (!direct) {
      for (const Val &arg : args) {
        if (arg.ptr) {
          poison(arg.ptr->param);
        }
      }
      Val v;
      v.ok = ok;
      return v;
    }
    const std::string key = functionKey(callee, sm);
    const unsigned n = std::min(call->getNumArgs(), callee->getNumParams());
    for (unsigned i = 0; i < args.size() && i < n; ++i) {
      if (!args[i].ptr) {
        continue;
      }
      if (key.empty() || args[i].ptr->off != 0 || args[i].ptr->ref) {
        poison(args[i].ptr->param);
        continue;
      }
      noteForward(args[i].ptr->param, key, i);
    }
    Val v;
    v.ok = ok;
    if (callee->getReturnType()->isPointerType() && !key.empty()) {
      v.isForward = true;
      v.fwdKey = key;
      v.fwdMap.assign(n, -1);
      for (unsigned i = 0; i < n && i < args.size(); ++i) {
        if (args[i].ptr && args[i].ptr->off == 0 && !args[i].ptr->ref) {
          v.fwdMap[i] = static_cast<int>(args[i].ptr->param);
        }
      }
    }
    return v;
  }

  void takeReturn(const Expr *e) {
    if (!cur) {
      return;
    }
    if (fn.getReturnType().isNull() || !fn.getReturnType()->isPointerType()) {
      if (e) {
        eval(e, false);
      }
      return;
    }
    if (!e) {
      cur->ret = Summary::Ret{};
      cur->ret.kind = Summary::Ret::Kind::Wild;
      return;
    }
    Val v = eval(e, false);
    if (!v.ok) {
      cur->ret.kind = Summary::Ret::Kind::Wild;
      return;
    }
    if (v.isNull) {
      cur->ret = Summary::Ret{};
      cur->ret.kind = Summary::Ret::Kind::Null;
      return;
    }
    if (v.ptr && !v.ptr->ref) {
      cur->ret = Summary::Ret{};
      cur->ret.kind = Summary::Ret::Kind::Param;
      cur->ret.param = v.ptr->param;
      cur->ret.off = v.ptr->off;
      return;
    }
    if (v.isForward) {
      cur->ret = Summary::Ret{};
      cur->ret.kind = Summary::Ret::Kind::Forward;
      cur->ret.fwdKey = std::move(v.fwdKey);
      cur->ret.fwdMap = std::move(v.fwdMap);
      return;
    }
    cur->ret.kind = Summary::Ret::Kind::Wild;
  }

  void transfer(const CFGBlock *b) {
    if (!cur) {
      return;
    }
    llvm::SmallVector<const Stmt *, 8> elems;
    for (const auto &el : *b) {
      if (std::optional<CFGStmt> cs = el.getAs<CFGStmt>()) {
        if (const Stmt *s = cs->getStmt()) {
          elems.push_back(s);
        }
      }
    }
    // A compound, if, or loop is a CFG element next to its children. It is not
    // a container that hides those children, and it is not a use of a parameter.
    auto wrapper = [](const Stmt *s) {
      return llvm::isa<clang::CompoundStmt>(s) || llvm::isa<IfStmt>(s) || llvm::isa<WhileStmt>(s) ||
             llvm::isa<ForStmt>(s) || llvm::isa<DoStmt>(s) || llvm::isa<clang::SwitchStmt>(s) ||
             llvm::isa<clang::CXXForRangeStmt>(s);
    };
    llvm::DenseMap<const Stmt *, char> have;
    for (const Stmt *s : elems) {
      if (!wrapper(s)) {
        have[s] = 1;
      }
    }
    for (const Stmt *s : elems) {
      if (wrapper(s)) {
        continue;
      }
      bool inner = false;
      if (parents) {
        for (const Stmt *p = parents->getParent(s); p; p = parents->getParent(p)) {
          if (have.find(p) != have.end()) {
            inner = true;
            break;
          }
        }
      }
      if (inner) {
        continue;
      }
      if (const auto *rs = llvm::dyn_cast<ReturnStmt>(s)) {
        takeReturn(rs->getRetValue());
        continue;
      }
      if (const auto *ds = llvm::dyn_cast<DeclStmt>(s)) {
        for (const Decl *d : ds->decls()) {
          const auto *vd = llvm::dyn_cast<VarDecl>(d);
          if (!vd) {
            continue;
          }
          vd = vd->getCanonicalDecl();
          cur->alias.erase(vd);
          if (!vd->getInit() || !vd->getType()->isPointerType()) {
            if (vd->getInit()) {
              eval(vd->getInit(), false);
            }
            continue;
          }
          Val init = eval(vd->getInit(), false);
          if (init.ok && init.ptr) {
            cur->alias[vd] = *init.ptr;
          }
        }
        continue;
      }
      if (const auto *e = llvm::dyn_cast<Expr>(s)) {
        eval(e, false);
      }
    }
    if (const Expr *cond = termCond(b->getTerminatorStmt())) {
      if (have.find(cond) == have.end()) {
        eval(cond, false);
      }
    }
  }
};

} // namespace

Summary summarizeFunction(const FunctionDecl &fn,
                          const CFG &cfg,
                          ASTContext &ctx,
                          const SourceManager &sm) {
  return Summarizer(fn, cfg, ctx, sm).run();
}
