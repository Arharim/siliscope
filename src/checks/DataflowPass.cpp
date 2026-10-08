#include "siliscope/DataflowCheck.h"
#include "siliscope/Report.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Analysis/Analyses/UninitializedValues.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

#include <cctype>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using clang::AnalysisDeclContext;
using clang::AnalysisDeclContextManager;
using clang::ArraySubscriptExpr;
using clang::ASTContext;
using clang::BinaryOperator;
using clang::BreakStmt;
using clang::CastExpr;
using clang::CFG;
using clang::CFGBlock;
using clang::CFGStmt;
using clang::CompoundStmt;
using clang::ConditionalOperator;
using clang::ConstantArrayType;
using clang::CXXBindTemporaryExpr;
using clang::CXXForRangeStmt;
using clang::CXXThisExpr;
using clang::CXXThrowExpr;
using clang::Decl;
using clang::DeclRefExpr;
using clang::DeclStmt;
using clang::DoStmt;
using clang::Expr;
using clang::ExprWithCleanups;
using clang::FileID;
using clang::ForStmt;
using clang::FunctionDecl;
using clang::GotoStmt;
using clang::IfStmt;
using clang::IndirectGotoStmt;
using clang::LambdaExpr;
using clang::MemberExpr;
using clang::NamedDecl;
using clang::ParmVarDecl;
using clang::QualType;
using clang::ReturnStmt;
using clang::SourceLocation;
using clang::SourceManager;
using clang::Stmt;
using clang::SwitchStmt;
using clang::UnaryOperator;
using clang::ValueDecl;
using clang::VarDecl;
using clang::WhileStmt;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isImplicit;
using clang::ast_matchers::unless;

namespace {

constexpr const char *kUninit = "ss.expr.uninit";
constexpr const char *kBounds = "ss.mem.bounds";
constexpr const char *kDangle = "ss.mem.no-dangling";
constexpr const char *kLoop = "ss.ctrl.loop-bound";
constexpr const char *kNull = "ss.mem.no-null-deref";
constexpr const char *kFree = "ss.mem.no-use-after-free";
constexpr const char *kOverlap = "ss.mem.no-overlap-copy";
constexpr const char *kNul = "ss.mem.string-room-for-nul";
constexpr const char *kOnePast = "ss.ptr.no-deref-one-past";
constexpr const char *kSame = "ss.ptr.same-array";
constexpr const char *kCopy = "ss.libc.copy-fits-dest";
constexpr const char *kDiv = "ss.conv.no-div-zero";
constexpr const char *kSigned = "ss.conv.no-signed-overflow";
constexpr const char *kUnseq = "ss.expr.no-unseq";
constexpr const char *kFinite = "ss.expr.fp-must-be-finite";
constexpr const char *kInvariant = "ss.ctrl.no-invariant-condition";

bool ident(const NamedDecl *d, std::string &out) {
  if (!d || !d->getDeclName().isIdentifier()) {
    return false;
  }
  out = d->getName().str();
  return true;
}

bool suppressed(Reporter &reporter, const char *id, const NamedDecl *d) {
  std::string name;
  if (!ident(d, name)) {
    return false;
  }
  return reporter.allows(id, name.c_str());
}

void emitNamed(Reporter &reporter,
               const SourceManager &sm,
               SourceLocation loc,
               const char *id,
               const char *msg,
               const NamedDecl *who) {
  if (who && suppressed(reporter, id, who)) {
    return;
  }
  reporter.emit(sm, loc, id, msg);
}

const VarDecl *plainVar(const Expr *e) {
  if (!e) {
    return nullptr;
  }
  const auto *dr = llvm::dyn_cast<DeclRefExpr>(e->IgnoreParenImpCasts());
  if (!dr) {
    return nullptr;
  }
  const auto *vd = llvm::dyn_cast<VarDecl>(dr->getDecl());
  return vd ? vd->getCanonicalDecl() : nullptr;
}

bool integral(const VarDecl *vd) {
  if (!vd) {
    return false;
  }
  QualType t = vd->getType();
  return !t.isNull() && t->isIntegralOrEnumerationType();
}

const VarDecl *plainInt(const Expr *e) {
  const VarDecl *vd = plainVar(e);
  return integral(vd) ? vd : nullptr;
}

bool mentions(const Stmt *s, const VarDecl *vd) {
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

bool isNest(const Stmt *s) {
  return llvm::isa<ForStmt>(s) || llvm::isa<WhileStmt>(s) || llvm::isa<DoStmt>(s) ||
         llvm::isa<CXXForRangeStmt>(s) || llvm::isa<SwitchStmt>(s);
}

bool exitsLoop(const Stmt *s, bool nested) {
  if (!s || llvm::isa<LambdaExpr>(s)) {
    return false;
  }
  if (llvm::isa<ReturnStmt>(s) || llvm::isa<GotoStmt>(s) || llvm::isa<IndirectGotoStmt>(s) ||
      llvm::isa<CXXThrowExpr>(s)) {
    return true;
  }
  if (llvm::isa<BreakStmt>(s)) {
    return !nested;
  }
  if (isNest(s)) {
    for (const Stmt *c : s->children()) {
      if (exitsLoop(c, true)) {
        return true;
      }
    }
    return false;
  }
  for (const Stmt *c : s->children()) {
    if (exitsLoop(c, nested)) {
      return true;
    }
  }
  return false;
}

struct Upd {
  bool any = false;
  bool dec = false;
};

void collectUpd(const Stmt *s, const VarDecl *vd, Upd &u) {
  if (!s || !vd || llvm::isa<LambdaExpr>(s)) {
    return;
  }
  if (llvm::isa<ForStmt>(s) || llvm::isa<WhileStmt>(s) || llvm::isa<DoStmt>(s) ||
      llvm::isa<CXXForRangeStmt>(s)) {
    return;
  }
  if (const auto *uo = llvm::dyn_cast<UnaryOperator>(s)) {
    if (uo->isIncrementDecrementOp() && plainInt(uo->getSubExpr()) == vd) {
      u.any = true;
      if (uo->isDecrementOp()) {
        u.dec = true;
      }
    }
  } else if (const auto *bo = llvm::dyn_cast<BinaryOperator>(s)) {
    auto op = bo->getOpcode();
    if ((op == clang::BO_Assign || op == clang::BO_AddAssign || op == clang::BO_SubAssign) &&
        plainInt(bo->getLHS()) == vd) {
      u.any = true;
      if (op == clang::BO_SubAssign) {
        u.dec = true;
      }
    }
  }
  for (const Stmt *c : s->children()) {
    collectUpd(c, vd, u);
  }
}

bool constantInt(const Expr *cond, ASTContext &ctx, bool &zero) {
  zero = false;
  if (!cond) {
    return false;
  }
  Expr::EvalResult value;
  if (!cond->EvaluateAsInt(value, ctx) || !value.Val.isInt()) {
    return false;
  }
  zero = value.Val.getInt().isZero();
  return true;
}

bool relationalCap(const Expr *cond, const Stmt *body, const Stmt *inc) {
  if (!cond) {
    return false;
  }
  const Expr *e = cond->IgnoreParenImpCasts();
  if (const auto *bo = llvm::dyn_cast<BinaryOperator>(e)) {
    if (bo->getOpcode() == clang::BO_LAnd || bo->getOpcode() == clang::BO_LOr) {
      return relationalCap(bo->getLHS(), body, inc) || relationalCap(bo->getRHS(), body, inc);
    }
    auto op = bo->getOpcode();
    if (bo->isRelationalOp() || op == clang::BO_NE) {
      const Expr *left = bo->getLHS();
      const Expr *right = bo->getRHS();
      const VarDecl *vars[2] = {plainInt(left), plainInt(right)};
      const Expr *other[2] = {right, left};
      for (int i = 0; i < 2; ++i) {
        if (!vars[i] || mentions(other[i], vars[i])) {
          continue;
        }
        Upd u;
        collectUpd(body, vars[i], u);
        collectUpd(inc, vars[i], u);
        if (u.any) {
          return true;
        }
      }
    }
  }
  return false;
}

bool countdown(const Expr *cond, const Stmt *body, const Stmt *inc) {
  const VarDecl *vd = plainInt(cond);
  if (!vd) {
    return false;
  }
  Upd u;
  collectUpd(body, vd, u);
  collectUpd(inc, vd, u);
  return u.dec;
}

bool capped(const Stmt *loop, ASTContext &ctx) {
  const Expr *cond = nullptr;
  const Stmt *body = nullptr;
  const Stmt *inc = nullptr;
  if (const auto *w = llvm::dyn_cast<WhileStmt>(loop)) {
    cond = w->getCond();
    body = w->getBody();
  } else if (const auto *d = llvm::dyn_cast<DoStmt>(loop)) {
    cond = d->getCond();
    body = d->getBody();
  } else if (const auto *f = llvm::dyn_cast<ForStmt>(loop)) {
    cond = f->getCond();
    body = f->getBody();
    inc = f->getInc();
  } else {
    return true;
  }
  bool zero = false;
  if (constantInt(cond, ctx, zero) && zero) {
    return true;
  }
  if (relationalCap(cond, body, inc) || countdown(cond, body, inc)) {
    return true;
  }
  // for(;;) and while(1) with no break or return are the idle superloop.
  const bool always = cond == nullptr || (constantInt(cond, ctx, zero) && !zero);
  return always && !exitsLoop(body, false);
}

SourceLocation loopLoc(const Stmt *s) {
  if (const auto *w = llvm::dyn_cast<WhileStmt>(s)) {
    return w->getWhileLoc();
  }
  if (const auto *d = llvm::dyn_cast<DoStmt>(s)) {
    return d->getDoLoc();
  }
  if (const auto *f = llvm::dyn_cast<ForStmt>(s)) {
    return f->getForLoc();
  }
  return s->getBeginLoc();
}

void walkLoops(const Stmt *s, ASTContext &ctx, Reporter &reporter, const SourceManager &sm) {
  if (!s || llvm::isa<LambdaExpr>(s)) {
    return;
  }
  if (llvm::isa<WhileStmt>(s) || llvm::isa<DoStmt>(s) || llvm::isa<ForStmt>(s)) {
    if (!capped(s, ctx)) {
      reporter.emit(sm, loopLoc(s), kLoop, "loop has no iteration cap");
    }
  }
  for (const Stmt *c : s->children()) {
    walkLoops(c, ctx, reporter, sm);
  }
}

void checkLoops(const FunctionDecl *fn,
                ASTContext &ctx,
                Reporter &reporter,
                const SourceManager &sm) {
  if (!fn->getBody()) {
    return;
  }
  if (fn->getDeclName().isIdentifier()) {
    const std::string name = fn->getName().str();
    if (reporter.allows(kLoop, name.c_str())) {
      return;
    }
  }
  walkLoops(fn->getBody(), ctx, reporter, sm);
}

bool fnAllowed(Reporter &reporter, const FunctionDecl *fn, const char *id) {
  if (!fn || !fn->getDeclName().isIdentifier()) {
    return false;
  }
  const std::string name = fn->getName().str();
  return reporter.allows(id, name.c_str());
}

bool hasTrait(const Stmt *s) {
  if (!s) {
    return false;
  }
  if (llvm::isa<clang::UnaryExprOrTypeTraitExpr>(s)) {
    return true;
  }
  for (const Stmt *c : s->children()) {
    if (hasTrait(c)) {
      return true;
    }
  }
  return false;
}

bool lineHas(const SourceManager &sm, FileID fid, unsigned line, const char *needle) {
  if (line == 0) {
    return false;
  }
  const SourceLocation begin = sm.translateLineCol(fid, line, 1);
  if (begin.isInvalid()) {
    return false;
  }
  bool invalid = false;
  const char *p = sm.getCharacterData(begin, &invalid);
  if (invalid || !p) {
    return false;
  }
  std::string text;
  for (int i = 0; i < 400 && p[i] != '\0' && p[i] != '\n' && p[i] != '\r'; ++i) {
    text.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(p[i]))));
  }
  return text.find(needle) != std::string::npos;
}

// A comment that says static_assert documents a compile-time guard.
bool documentedGuard(const SourceManager &sm, SourceLocation loc) {
  loc = sm.getSpellingLoc(loc);
  if (loc.isInvalid()) {
    return false;
  }
  const unsigned line = sm.getSpellingLineNumber(loc);
  const FileID fid = sm.getFileID(loc);
  if (lineHas(sm, fid, line, "static_assert")) {
    return true;
  }
  return line > 1 && lineHas(sm, fid, line - 1, "static_assert");
}

bool alwaysBool(const Expr *cond, bool &zero) {
  const Expr *e = cond->IgnoreParenImpCasts();
  if (const auto *b = llvm::dyn_cast<clang::CXXBoolLiteralExpr>(e)) {
    zero = !b->getValue();
    return true;
  }
  return false;
}

void walkInvariant(const Stmt *s, ASTContext &ctx, Reporter &reporter, const SourceManager &sm) {
  if (!s || llvm::isa<LambdaExpr>(s)) {
    return;
  }
  const Expr *cond = nullptr;
  bool loop = false;
  if (const auto *i = llvm::dyn_cast<IfStmt>(s)) {
    // if constexpr is a compile-time branch. Its condition is required to fold.
    if (!i->isConstexpr()) {
      cond = i->getCond();
    }
  } else if (const auto *w = llvm::dyn_cast<WhileStmt>(s)) {
    cond = w->getCond();
    loop = true;
  } else if (const auto *d = llvm::dyn_cast<DoStmt>(s)) {
    cond = d->getCond();
    loop = true;
  } else if (const auto *f = llvm::dyn_cast<ForStmt>(s)) {
    cond = f->getCond();
    loop = true;
  } else if (const auto *sw = llvm::dyn_cast<SwitchStmt>(s)) {
    cond = sw->getCond();
  } else if (const auto *c = llvm::dyn_cast<ConditionalOperator>(s)) {
    cond = c->getCond();
  }
  if (cond && !hasTrait(cond) && !documentedGuard(sm, cond->getExprLoc())) {
    bool zero = false;
    const bool folded = constantInt(cond, ctx, zero) || alwaysBool(cond, zero);
    // while (0) / do while (0) is the empty macro idiom, not a dead branch.
    if (folded && !(loop && zero)) {
      reporter.emit(sm, cond->getExprLoc(), kInvariant, "condition is always true or always false");
    }
  }
  for (const Stmt *c : s->children()) {
    walkInvariant(c, ctx, reporter, sm);
  }
}

void checkInvariant(const FunctionDecl *fn,
                    ASTContext &ctx,
                    Reporter &reporter,
                    const SourceManager &sm) {
  if (!fn->getBody() || fnAllowed(reporter, fn, kInvariant)) {
    return;
  }
  walkInvariant(fn->getBody(), ctx, reporter, sm);
}

struct Acc {
  const VarDecl *vd = nullptr;
  bool mod = false;
  SourceLocation loc;
};

const Expr *bareExpr(const Expr *e) {
  while (e) {
    if (const auto *p = llvm::dyn_cast<clang::ParenExpr>(e)) {
      e = p->getSubExpr();
      continue;
    }
    if (const auto *f = llvm::dyn_cast<clang::FullExpr>(e)) {
      e = f->getSubExpr();
      continue;
    }
    if (const auto *c = llvm::dyn_cast<CastExpr>(e)) {
      const auto k = c->getCastKind();
      if (k == clang::CK_NoOp || k == clang::CK_LValueToRValue || k == clang::CK_IntegralCast ||
          k == clang::CK_FloatingCast || k == clang::CK_BitCast) {
        e = c->getSubExpr();
        continue;
      }
    }
    break;
  }
  return e;
}

const VarDecl *scalarVar(const Expr *e) {
  const VarDecl *vd = plainVar(bareExpr(e));
  if (!vd || vd->getType().isNull() || vd->getType().isVolatileQualified()) {
    return nullptr;
  }
  if (!vd->getType()->isScalarType()) {
    return nullptr;
  }
  return vd;
}

class UnseqWalk {
public:
  UnseqWalk(Reporter &reporter, const SourceManager &sm) : reporter(reporter), sm(sm) {}

  void walk(const Stmt *s) {
    if (!s || llvm::isa<LambdaExpr>(s)) {
      return;
    }
    if (const auto *i = llvm::dyn_cast<IfStmt>(s)) {
      region(i->getCond());
      if (const Stmt *init = i->getInit()) {
        walk(init);
      }
      walk(i->getThen());
      walk(i->getElse());
      return;
    }
    if (const auto *w = llvm::dyn_cast<WhileStmt>(s)) {
      region(w->getCond());
      walk(w->getBody());
      return;
    }
    if (const auto *d = llvm::dyn_cast<DoStmt>(s)) {
      walk(d->getBody());
      region(d->getCond());
      return;
    }
    if (const auto *f = llvm::dyn_cast<ForStmt>(s)) {
      if (const Stmt *init = f->getInit()) {
        walk(init);
      }
      region(f->getCond());
      region(f->getInc());
      walk(f->getBody());
      return;
    }
    if (const auto *sw = llvm::dyn_cast<SwitchStmt>(s)) {
      if (const Stmt *init = sw->getInit()) {
        walk(init);
      }
      region(sw->getCond());
      walk(sw->getBody());
      return;
    }
    if (const auto *rs = llvm::dyn_cast<ReturnStmt>(s)) {
      region(rs->getRetValue());
      return;
    }
    if (const auto *ds = llvm::dyn_cast<DeclStmt>(s)) {
      for (const Decl *d : ds->decls()) {
        if (const auto *vd = llvm::dyn_cast<VarDecl>(d)) {
          region(vd->getInit());
        }
      }
      return;
    }
    if (const auto *e = llvm::dyn_cast<Expr>(s)) {
      region(e);
      return;
    }
    for (const Stmt *c : s->children()) {
      walk(c);
    }
  }

private:
  Reporter &reporter;
  const SourceManager &sm;

  void region(const Expr *e) {
    if (!e) {
      return;
    }
    std::vector<Acc> hits;
    collect(e, hits);
    report(hits);
  }

  void report(const std::vector<Acc> &hits) {
    for (size_t i = 0; i < hits.size(); ++i) {
      const VarDecl *vd = hits[i].vd;
      if (!vd) {
        continue;
      }
      bool seen = false;
      for (size_t k = 0; k < i; ++k) {
        if (hits[k].vd == vd) {
          seen = true;
          break;
        }
      }
      if (seen) {
        continue;
      }
      int mods = 0;
      int reads = 0;
      SourceLocation at;
      for (const Acc &h : hits) {
        if (h.vd != vd) {
          continue;
        }
        if (h.mod) {
          ++mods;
          at = h.loc;
        } else {
          ++reads;
          if (mods > 0) {
            at = h.loc;
          }
        }
      }
      if (mods >= 2 || (mods == 1 && reads >= 1)) {
        emitNamed(reporter, sm, at, kUnseq, "unsequenced side effect on this object", vd);
      }
    }
  }

  void collect(const Expr *e, std::vector<Acc> &hits) {
    e = bareExpr(e);
    if (!e) {
      return;
    }
    if (const auto *bo = llvm::dyn_cast<BinaryOperator>(e)) {
      const auto op = bo->getOpcode();
      // Each side is sequenced against the other. Effects do not pair across the operator.
      if (op == clang::BO_Comma || op == clang::BO_LAnd || op == clang::BO_LOr) {
        region(bo->getLHS());
        region(bo->getRHS());
        return;
      }
      if (op == clang::BO_Assign) {
        std::vector<Acc> rhs;
        collect(bo->getRHS(), rhs);
        const VarDecl *slot = nullptr;
        std::vector<Acc> lhs;
        collectLvalue(bo->getLHS(), lhs, slot);
        if (slot) {
          bool rhsMod = false;
          for (const Acc &h : rhs) {
            if (h.vd == slot && h.mod) {
              rhsMod = true;
              break;
            }
          }
          if (!rhsMod) {
            std::vector<Acc> kept;
            for (const Acc &h : rhs) {
              if (!(h.vd == slot && !h.mod)) {
                kept.push_back(h);
              }
            }
            rhs.swap(kept);
          }
        }
        hits.insert(hits.end(), rhs.begin(), rhs.end());
        hits.insert(hits.end(), lhs.begin(), lhs.end());
        if (slot) {
          hits.push_back(Acc{slot, true, bo->getOperatorLoc()});
        }
        return;
      }
      if (bo->isCompoundAssignmentOp()) {
        collect(bo->getRHS(), hits);
        const VarDecl *slot = nullptr;
        collectLvalue(bo->getLHS(), hits, slot);
        if (slot) {
          hits.push_back(Acc{slot, true, bo->getOperatorLoc()});
        }
        return;
      }
    }
    if (const auto *co = llvm::dyn_cast<ConditionalOperator>(e)) {
      region(co->getCond());
      region(co->getTrueExpr());
      region(co->getFalseExpr());
      return;
    }
    if (const auto *call = llvm::dyn_cast<clang::CallExpr>(e)) {
      collect(call->getCallee(), hits);
      for (const Expr *arg : call->arguments()) {
        collect(arg, hits);
      }
      return;
    }
    if (const auto *uo = llvm::dyn_cast<UnaryOperator>(e)) {
      if (uo->isIncrementDecrementOp()) {
        const VarDecl *slot = nullptr;
        collectLvalue(uo->getSubExpr(), hits, slot);
        if (slot) {
          hits.push_back(Acc{slot, true, uo->getOperatorLoc()});
        }
        return;
      }
    }
    if (const VarDecl *vd = scalarVar(e)) {
      hits.push_back(Acc{vd, false, e->getExprLoc()});
      return;
    }
    for (const Stmt *c : e->children()) {
      if (const auto *ce = llvm::dyn_cast_or_null<Expr>(c)) {
        collect(ce, hits);
      }
    }
  }

  void collectLvalue(const Expr *e, std::vector<Acc> &hits, const VarDecl *&slot) {
    e = bareExpr(e);
    if (!e) {
      return;
    }
    if (const VarDecl *vd = scalarVar(e)) {
      slot = vd;
      return;
    }
    if (const auto *sub = llvm::dyn_cast<ArraySubscriptExpr>(e)) {
      collect(sub->getBase(), hits);
      collect(sub->getIdx(), hits);
      return;
    }
    if (const auto *uo = llvm::dyn_cast<UnaryOperator>(e)) {
      if (uo->getOpcode() == clang::UO_Deref) {
        collect(uo->getSubExpr(), hits);
        return;
      }
    }
    if (const auto *me = llvm::dyn_cast<MemberExpr>(e)) {
      collect(me->getBase(), hits);
      return;
    }
    collect(e, hits);
  }
};

void checkUnseq(const FunctionDecl *fn, Reporter &reporter, const SourceManager &sm) {
  if (!fn->getBody()) {
    return;
  }
  UnseqWalk(reporter, sm).walk(fn->getBody());
}

class UninitHandler final : public clang::UninitVariablesHandler {
public:
  UninitHandler(Reporter &reporter, const SourceManager &sm) : reporter(reporter), sm(sm) {}

  void handleUseOfUninitVariable(const VarDecl *vd, const clang::UninitUse &use) override {
    if (!track(vd) || use.isConstRefOrPtrUse() || !use.getUser()) {
      return;
    }
    emitNamed(
        reporter, sm, use.getUser()->getExprLoc(), kUninit, "read of an uninitialized object", vd);
  }

  void handleSelfInit(const VarDecl *vd) override {
    if (!track(vd)) {
      return;
    }
    SourceLocation at = vd->getInit() ? vd->getInit()->getExprLoc() : vd->getLocation();
    emitNamed(reporter, sm, at, kUninit, "object is initialized from itself", vd);
  }

private:
  bool track(const VarDecl *vd) const {
    return vd && !llvm::isa<ParmVarDecl>(vd) && vd->hasLocalStorage() && !vd->isImplicit();
  }

  Reporter &reporter;
  const SourceManager &sm;
};

void checkUninit(const FunctionDecl &fn,
                 const CFG &cfg,
                 AnalysisDeclContext &ac,
                 Reporter &reporter,
                 const SourceManager &sm) {
  UninitHandler handler(reporter, sm);
  clang::UninitVariablesAnalysisStats stats = {};
  clang::runUninitializedVariablesAnalysis(fn, cfg, ac, handler, stats);
  (void)stats;
}

using VarList = llvm::SmallVector<const VarDecl *, 4>;
using IntList = llvm::SmallVector<llvm::APSInt, 4>;

struct ArrRef {
  const ValueDecl *base = nullptr;
  llvm::APSInt off;
  bool operator==(const ArrRef &o) const { return base == o.base && off == o.off; }
};

enum class NullFact { Must, Not };
enum class FpFact { Finite, NonFinite };

struct State {
  llvm::DenseMap<const VarDecl *, VarList> ptrs;
  llvm::DenseMap<const VarDecl *, IntList> ints;
  llvm::DenseMap<const VarDecl *, ArrRef> arrs;
  llvm::DenseMap<const VarDecl *, NullFact> nulls;
  llvm::DenseMap<const VarDecl *, FpFact> fps;
  llvm::DenseSet<const VarDecl *> freed;
};

struct ArrBound {
  const ValueDecl *base = nullptr;
  const ConstantArrayType *type = nullptr;
  llvm::APSInt off;
};

llvm::APSInt i64(int64_t v) {
  return llvm::APSInt(llvm::APInt(64, static_cast<uint64_t>(v), true), false);
}

std::optional<llvm::APSInt> norm(llvm::APSInt v) {
  if (v.getBitWidth() > 64) {
    if (v.getSignificantBits() > 64) {
      return std::nullopt;
    }
    v = v.trunc(64);
  } else if (v.getBitWidth() < 64) {
    v = v.extend(64);
  }
  if (v.isUnsigned()) {
    if (v.isSignBitSet()) {
      return std::nullopt;
    }
    v.setIsSigned(true);
  }
  return v;
}

void addVar(VarList &xs, const VarDecl *v) {
  if (!v) {
    return;
  }
  v = v->getCanonicalDecl();
  if (!llvm::is_contained(xs, v)) {
    xs.push_back(v);
  }
}

void addVars(VarList &xs, const VarList &more) {
  for (const VarDecl *v : more) {
    addVar(xs, v);
  }
}

bool sameVars(const VarList &a, const VarList &b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (const VarDecl *v : a) {
    if (!llvm::is_contained(b, v)) {
      return false;
    }
  }
  return true;
}

bool addInt(IntList &xs, llvm::APSInt v) {
  auto n = norm(std::move(v));
  if (!n) {
    return false;
  }
  for (const auto &e : xs) {
    if (e == *n) {
      return true;
    }
  }
  if (xs.size() >= 4) {
    return false;
  }
  xs.push_back(*n);
  return true;
}

bool sameInts(const IntList &a, const IntList &b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (const auto &v : a) {
    bool found = false;
    for (const auto &w : b) {
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

bool sameFacts(const llvm::DenseMap<const VarDecl *, NullFact> &a,
               const llvm::DenseMap<const VarDecl *, NullFact> &b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (const auto &kv : a) {
    auto it = b.find(kv.first);
    if (it == b.end() || it->second != kv.second) {
      return false;
    }
  }
  return true;
}

bool sameFacts(const llvm::DenseMap<const VarDecl *, FpFact> &a,
               const llvm::DenseMap<const VarDecl *, FpFact> &b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (const auto &kv : a) {
    auto it = b.find(kv.first);
    if (it == b.end() || it->second != kv.second) {
      return false;
    }
  }
  return true;
}

bool sameFreed(const llvm::DenseSet<const VarDecl *> &a, const llvm::DenseSet<const VarDecl *> &b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (const VarDecl *v : a) {
    if (!b.contains(v)) {
      return false;
    }
  }
  return true;
}

bool sameState(const State &a, const State &b) {
  if (a.ptrs.size() != b.ptrs.size() || a.ints.size() != b.ints.size() ||
      a.arrs.size() != b.arrs.size() || a.nulls.size() != b.nulls.size() ||
      a.fps.size() != b.fps.size() || a.freed.size() != b.freed.size()) {
    return false;
  }
  for (const auto &kv : a.ptrs) {
    auto it = b.ptrs.find(kv.first);
    if (it == b.ptrs.end() || !sameVars(kv.second, it->second)) {
      return false;
    }
  }
  for (const auto &kv : a.ints) {
    auto it = b.ints.find(kv.first);
    if (it == b.ints.end() || !sameInts(kv.second, it->second)) {
      return false;
    }
  }
  for (const auto &kv : a.arrs) {
    auto it = b.arrs.find(kv.first);
    if (it == b.arrs.end() || !(kv.second == it->second)) {
      return false;
    }
  }
  return sameFacts(a.nulls, b.nulls) && sameFacts(a.fps, b.fps) && sameFreed(a.freed, b.freed);
}

bool transparentCast(clang::CastKind k) {
  switch (k) {
  case clang::CK_NoOp:
  case clang::CK_LValueToRValue:
  case clang::CK_BitCast:
  case clang::CK_LValueBitCast:
  case clang::CK_IntegralCast:
  case clang::CK_NullToPointer:
    return true;
  default:
    return false;
  }
}

const Expr *peel(const Expr *e) {
  while (e) {
    if (const auto *p = llvm::dyn_cast<clang::ParenExpr>(e)) {
      e = p->getSubExpr();
      continue;
    }
    if (const auto *m = llvm::dyn_cast<clang::MaterializeTemporaryExpr>(e)) {
      e = m->getSubExpr();
      continue;
    }
    if (const auto *b = llvm::dyn_cast<CXXBindTemporaryExpr>(e)) {
      e = b->getSubExpr();
      continue;
    }
    if (const auto *c = llvm::dyn_cast<ExprWithCleanups>(e)) {
      e = c->getSubExpr();
      continue;
    }
    if (const auto *c = llvm::dyn_cast<CastExpr>(e)) {
      if (c->getCastKind() == clang::CK_ArrayToPointerDecay) {
        return e;
      }
      if (transparentCast(c->getCastKind())) {
        e = c->getSubExpr();
        continue;
      }
    }
    break;
  }
  return e;
}

const Stmt *peelStmt(const Stmt *s) {
  while (const auto *e = llvm::dyn_cast_or_null<Expr>(s)) {
    if (const auto *c = llvm::dyn_cast<ExprWithCleanups>(e)) {
      s = c->getSubExpr();
      continue;
    }
    if (const auto *b = llvm::dyn_cast<CXXBindTemporaryExpr>(e)) {
      s = b->getSubExpr();
      continue;
    }
    if (const auto *p = llvm::dyn_cast<clang::ParenExpr>(e)) {
      s = p->getSubExpr();
      continue;
    }
    if (const auto *c = llvm::dyn_cast<CastExpr>(e)) {
      if (c->getCastKind() == clang::CK_NoOp) {
        s = c->getSubExpr();
        continue;
      }
    }
    break;
  }
  return s;
}

class Lattice {
public:
  Lattice(const FunctionDecl *fn, ASTContext &ctx, Reporter &reporter, const SourceManager &sm)
      : ctx(ctx), reporter(reporter), sm(sm), fn(fn) {
    if (fn->getBody()) {
      walkScope(fn->getBody(), nullptr);
    }
  }

  void run(const CFG &cfg) {
    const unsigned n = cfg.getNumBlockIDs();
    outs.clear();
    outs.resize(n);
    ready.assign(n, 0);
    reach.assign(n, 0);
    queued.assign(n, 0);
    std::vector<const CFGBlock *> work;
    auto enqueue = [&](const CFGBlock *b) {
      if (!b) {
        return;
      }
      const unsigned id = b->getBlockID();
      reach[id] = 1;
      if (queued[id]) {
        return;
      }
      queued[id] = 1;
      work.push_back(b);
    };
    enqueue(&cfg.getEntry());
    unsigned steps = 0;
    const unsigned limit = n * 48 + 8;
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
      bool partial = false;
      State in = join(b, partial);
      State next = in;
      st = &next;
      for (const auto &el : *b) {
        if (std::optional<CFGStmt> cs = el.getAs<CFGStmt>()) {
          transfer(cs->getStmt(), false);
        }
      }
      if (!ready[id] || !sameState(next, outs[id])) {
        outs[id] = std::move(next);
        ready[id] = 1;
        for (const CFGBlock *succ : b->succs()) {
          enqueue(succ);
        }
      }
    }
    if (gaveUp) {
      return;
    }
    for (const CFGBlock *b : cfg) {
      if (!b || !reach[b->getBlockID()] || !ready[b->getBlockID()]) {
        continue;
      }
      bool partial = false;
      State in = join(b, partial);
      if (partial) {
        continue;
      }
      st = &in;
      for (const auto &el : *b) {
        if (std::optional<CFGStmt> cs = el.getAs<CFGStmt>()) {
          transfer(cs->getStmt(), true);
        }
      }
      if (const Expr *cond = termExpr(b->getTerminatorStmt())) {
        scan(cond);
      }
    }
  }

private:
  ASTContext &ctx;
  Reporter &reporter;
  const SourceManager &sm;
  const FunctionDecl *fn = nullptr;
  State *st = nullptr;
  bool inFiniteTest = false;
  bool suppressFreed = false;
  std::vector<State> outs;
  std::vector<char> ready;
  std::vector<char> reach;
  std::vector<char> queued;
  llvm::DenseMap<const VarDecl *, const Stmt *> scopes;
  llvm::DenseMap<const Stmt *, const Stmt *> parents;

  void record(const DeclStmt *ds, const Stmt *scope) {
    for (const Decl *d : ds->decls()) {
      const auto *vd = llvm::dyn_cast<VarDecl>(d);
      if (!vd || !vd->hasLocalStorage()) {
        continue;
      }
      scopes[vd->getCanonicalDecl()] = scope;
    }
  }

  void walkScope(const Stmt *s, const Stmt *cur) {
    if (!s || llvm::isa<LambdaExpr>(s)) {
      return;
    }
    if (const auto *comp = llvm::dyn_cast<CompoundStmt>(s)) {
      parents[comp] = cur;
      for (const Stmt *c : comp->body()) {
        if (const auto *ds = llvm::dyn_cast<DeclStmt>(c)) {
          record(ds, comp);
        }
        walkScope(c, comp);
      }
      return;
    }
    if (const auto *fs = llvm::dyn_cast<ForStmt>(s)) {
      parents[fs] = cur;
      if (const auto *ds = llvm::dyn_cast_or_null<DeclStmt>(fs->getInit())) {
        record(ds, fs);
      }
      walkScope(fs->getBody(), fs);
      return;
    }
    for (const Stmt *c : s->children()) {
      walkScope(c, cur);
    }
  }

  const Stmt *scopeOf(const VarDecl *vd) const {
    auto it = scopes.find(vd->getCanonicalDecl());
    return it == scopes.end() ? nullptr : it->second;
  }

  // outer strictly wraps inner. nullptr is the function frame (parameters).
  bool encloses(const Stmt *outer, const Stmt *inner) const {
    if (outer == inner) {
      return false;
    }
    const Stmt *p = inner;
    for (int guard = 0; p && guard < 64; ++guard) {
      auto it = parents.find(p);
      if (it == parents.end()) {
        return outer == nullptr;
      }
      p = it->second;
      if (p == outer) {
        return true;
      }
    }
    return outer == nullptr && inner != nullptr;
  }

  bool slotOutlives(const VarDecl *slot, const VarList &held) const {
    if (!slot->hasLocalStorage()) {
      return true;
    }
    const Stmt *ss = scopeOf(slot);
    for (const VarDecl *t : held) {
      if (encloses(ss, scopeOf(t))) {
        return true;
      }
    }
    return false;
  }

  void flagDangling(const VarList &held, SourceLocation loc) {
    for (const VarDecl *vd : held) {
      emitNamed(reporter, sm, loc, kDangle, "address of an automatic object outlives it", vd);
    }
  }

  std::optional<IntList> evalInt(const Expr *e) {
    if (!e) {
      return std::nullopt;
    }
    e = e->IgnoreParenImpCasts();
    if (const VarDecl *vd = plainVar(e)) {
      if (integral(vd) && (vd->hasLocalStorage() || llvm::isa<ParmVarDecl>(vd))) {
        auto it = st->ints.find(vd);
        if (it == st->ints.end()) {
          return std::nullopt;
        }
        return it->second;
      }
    }
    // Fold + - * / % << in 64 bits before EvaluateAsInt. That API wraps into
    // the expression type, so a signed 1 << 31 looks like it fits in int.
    if (const auto *uo = llvm::dyn_cast<UnaryOperator>(e)) {
      if (uo->getOpcode() == clang::UO_Minus || uo->getOpcode() == clang::UO_Plus) {
        auto inner = evalInt(uo->getSubExpr());
        if (!inner) {
          return std::nullopt;
        }
        if (uo->getOpcode() == clang::UO_Plus) {
          return inner;
        }
        IntList out;
        for (const auto &v : *inner) {
          if (!addInt(out, -v)) {
            return std::nullopt;
          }
        }
        return out;
      }
    }
    if (const auto *bo = llvm::dyn_cast<BinaryOperator>(e)) {
      auto op = bo->getOpcode();
      if (op == clang::BO_Add || op == clang::BO_Sub || op == clang::BO_Mul ||
          op == clang::BO_Div || op == clang::BO_Rem || op == clang::BO_Shl) {
        auto L = evalInt(bo->getLHS());
        auto R = evalInt(bo->getRHS());
        if (!L || !R || L->size() * R->size() > 4) {
          return std::nullopt;
        }
        IntList out;
        for (const auto &a : *L) {
          for (const auto &b : *R) {
            llvm::APSInt v = a;
            if (op == clang::BO_Add) {
              v = a + b;
            } else if (op == clang::BO_Sub) {
              v = a - b;
            } else if (op == clang::BO_Mul) {
              bool overflow = false;
              v = a.smul_ov(b, overflow);
              if (overflow) {
                return std::nullopt;
              }
            } else if (op == clang::BO_Div || op == clang::BO_Rem) {
              if (b.isZero()) {
                return std::nullopt;
              }
              if (a.isMinSignedValue() && b.isAllOnes()) {
                return std::nullopt;
              }
              v = op == clang::BO_Div ? a.sdiv(b) : a.srem(b);
            } else {
              if (a.isNegative() || b.isNegative() || b.getActiveBits() > 6 || b.uge(63)) {
                return std::nullopt;
              }
              v = a.shl(static_cast<unsigned>(b.getZExtValue()));
            }
            if (!addInt(out, std::move(v))) {
              return std::nullopt;
            }
          }
        }
        return out;
      }
    }
    Expr::EvalResult folded;
    if (e->EvaluateAsInt(folded, ctx) && folded.Val.isInt()) {
      IntList one;
      if (!addInt(one, folded.Val.getInt())) {
        return std::nullopt;
      }
      return one;
    }
    return std::nullopt;
  }

  VarList objectAuto(const Expr *e) {
    VarList out;
    if (!e) {
      return out;
    }
    e = e->IgnoreParenImpCasts();
    if (const VarDecl *vd = plainVar(e)) {
      if (vd->hasLocalStorage()) {
        addVar(out, vd);
      }
      return out;
    }
    if (const auto *sub = llvm::dyn_cast<ArraySubscriptExpr>(e)) {
      return objectAuto(sub->getBase());
    }
    if (const auto *uo = llvm::dyn_cast<UnaryOperator>(e)) {
      if (uo->getOpcode() == clang::UO_Deref) {
        return autosOf(uo->getSubExpr());
      }
    }
    return out;
  }

  VarList autosOf(const Expr *e) {
    VarList out;
    if (!e) {
      return out;
    }
    e = peel(e);
    if (!e) {
      return out;
    }
    if (const auto *c = llvm::dyn_cast<CastExpr>(e)) {
      if (c->getCastKind() == clang::CK_ArrayToPointerDecay) {
        return objectAuto(c->getSubExpr());
      }
    }
    if (const auto *uo = llvm::dyn_cast<UnaryOperator>(e)) {
      if (uo->getOpcode() == clang::UO_AddrOf) {
        return objectAuto(uo->getSubExpr());
      }
    }
    if (const VarDecl *vd = plainVar(e)) {
      if (vd->getType()->isPointerType() || vd->getType()->isReferenceType()) {
        auto it = st->ptrs.find(vd);
        if (it != st->ptrs.end()) {
          return it->second;
        }
      }
      return out;
    }
    if (const auto *bo = llvm::dyn_cast<BinaryOperator>(e)) {
      if (bo->getOpcode() == clang::BO_Comma) {
        return autosOf(bo->getRHS());
      }
      if (bo->getOpcode() == clang::BO_Add || bo->getOpcode() == clang::BO_Sub) {
        VarList l = autosOf(bo->getLHS());
        if (!l.empty()) {
          return l;
        }
        if (bo->getOpcode() == clang::BO_Add) {
          return autosOf(bo->getRHS());
        }
      }
    }
    if (const auto *co = llvm::dyn_cast<ConditionalOperator>(e)) {
      addVars(out, autosOf(co->getTrueExpr()));
      addVars(out, autosOf(co->getFalseExpr()));
    }
    return out;
  }

  std::optional<ArrBound> fromRef(const ArrRef &r) const {
    if (!r.base) {
      return std::nullopt;
    }
    const auto *cat = ctx.getAsConstantArrayType(r.base->getType());
    if (!cat) {
      return std::nullopt;
    }
    ArrBound b;
    b.base = r.base;
    b.type = cat;
    b.off = r.off;
    return b;
  }

  std::optional<ArrBound> boundOf(const Expr *e) {
    if (!e) {
      return std::nullopt;
    }
    e = peel(e);
    if (!e) {
      return std::nullopt;
    }
    if (const auto *c = llvm::dyn_cast<CastExpr>(e)) {
      if (c->getCastKind() == clang::CK_ArrayToPointerDecay) {
        e = c->getSubExpr()->IgnoreParenImpCasts();
      }
    }
    if (const auto *cat = ctx.getAsConstantArrayType(e->getType())) {
      ArrBound b;
      b.type = cat;
      b.off = i64(0);
      if (const auto *dr = llvm::dyn_cast<DeclRefExpr>(e)) {
        b.base = llvm::dyn_cast<ValueDecl>(dr->getDecl());
      } else if (const auto *me = llvm::dyn_cast<MemberExpr>(e)) {
        b.base = llvm::dyn_cast<ValueDecl>(me->getMemberDecl());
      }
      if (const auto *vd = llvm::dyn_cast_or_null<VarDecl>(b.base)) {
        b.base = vd->getCanonicalDecl();
      }
      return b;
    }
    if (const VarDecl *vd = plainVar(e)) {
      auto it = st->arrs.find(vd);
      if (it == st->arrs.end()) {
        return std::nullopt;
      }
      return fromRef(it->second);
    }
    if (const auto *bo = llvm::dyn_cast<BinaryOperator>(e)) {
      auto op = bo->getOpcode();
      if (op == clang::BO_Add || op == clang::BO_Sub) {
        auto apply = [&](std::optional<ArrBound> b, const Expr *delta, bool sub) {
          if (!b) {
            return std::optional<ArrBound>{};
          }
          auto vals = evalInt(delta);
          if (!vals || vals->size() != 1) {
            return std::optional<ArrBound>{};
          }
          auto n = norm(sub ? (b->off - (*vals)[0]) : (b->off + (*vals)[0]));
          if (!n) {
            return std::optional<ArrBound>{};
          }
          b->off = *n;
          return b;
        };
        if (auto b = apply(boundOf(bo->getLHS()), bo->getRHS(), op == clang::BO_Sub)) {
          return b;
        }
        if (op == clang::BO_Add) {
          return apply(boundOf(bo->getRHS()), bo->getLHS(), false);
        }
      }
    }
    if (const auto *uo = llvm::dyn_cast<UnaryOperator>(e)) {
      if (uo->getOpcode() == clang::UO_AddrOf) {
        const auto *sub =
            llvm::dyn_cast<ArraySubscriptExpr>(uo->getSubExpr()->IgnoreParenImpCasts());
        if (!sub) {
          return std::nullopt;
        }
        auto b = boundOf(sub->getBase());
        auto vals = evalInt(sub->getIdx());
        if (!b || !vals || vals->size() != 1) {
          return std::nullopt;
        }
        auto n = norm(b->off + (*vals)[0]);
        if (!n) {
          return std::nullopt;
        }
        b->off = *n;
        return b;
      }
    }
    return std::nullopt;
  }

  uint64_t arrayCount(const ConstantArrayType *cat) const {
    const llvm::APInt &sz = cat->getSize();
    if (sz.getActiveBits() > 64) {
      return std::numeric_limits<uint64_t>::max();
    }
    return sz.getZExtValue();
  }

  bool oob(const llvm::APSInt &off, const llvm::APSInt &idx, uint64_t size) const {
    auto o = norm(off);
    auto n = norm(idx);
    if (!o || !n || o->isNegative() || n->isNegative()) {
      return true;
    }
    const uint64_t a = o->getZExtValue();
    const uint64_t b = n->getZExtValue();
    if (a > std::numeric_limits<uint64_t>::max() - b) {
      return true;
    }
    return a + b >= size;
  }

  void flagBounds(const ValueDecl *base, SourceLocation loc) {
    emitNamed(reporter, sm, loc, kBounds, "index is outside the array", base);
  }

  void emitRule(SourceLocation loc, const char *id, const char *msg, const NamedDecl *who) {
    if (fnAllowed(reporter, fn, id)) {
      return;
    }
    emitNamed(reporter, sm, loc, id, msg, who);
  }

  bool fitsSigned(const llvm::APSInt &v, QualType t) const {
    if (t.isNull() || !t->isSignedIntegerType()) {
      return true;
    }
    const unsigned bits = ctx.getIntWidth(t);
    if (bits == 0 || bits >= 64) {
      return true;
    }
    const int64_t sv = v.getSExtValue();
    const int64_t min = -(static_cast<int64_t>(1) << (bits - 1));
    const int64_t max = (static_cast<int64_t>(1) << (bits - 1)) - 1;
    return sv >= min && sv <= max;
  }

  void noteSigned(const Expr *e, SourceLocation loc) {
    if (!e || e->getType().isNull() || !e->getType()->isSignedIntegerType()) {
      return;
    }
    auto vals = evalInt(e);
    if (!vals || vals->size() != 1) {
      return;
    }
    if (!fitsSigned((*vals)[0], e->getType())) {
      emitRule(loc, kSigned, "signed arithmetic overflows", nullptr);
    }
  }

  void noteStored(const VarDecl *vd, SourceLocation loc) {
    if (!vd || !integral(vd)) {
      return;
    }
    auto it = st->ints.find(vd);
    if (it == st->ints.end() || it->second.size() != 1) {
      return;
    }
    if (!fitsSigned(it->second[0], vd->getType())) {
      emitRule(loc, kSigned, "signed arithmetic overflows", vd);
    }
  }

  bool isNullConst(const Expr *e) const {
    e = peel(e);
    if (!e) {
      return false;
    }
    if (llvm::isa<clang::CXXNullPtrLiteralExpr>(e) || llvm::isa<clang::GNUNullExpr>(e)) {
      return true;
    }
    if (const auto *lit = llvm::dyn_cast<clang::IntegerLiteral>(e)) {
      return lit->getValue().isZero();
    }
    Expr::EvalResult value;
    return e->EvaluateAsInt(value, ctx) && value.Val.isInt() && value.Val.getInt().isZero();
  }

  bool obviousNonNull(const Expr *e) const {
    e = peel(e);
    if (!e) {
      return false;
    }
    if (llvm::isa<clang::StringLiteral>(e)) {
      return true;
    }
    if (const auto *c = llvm::dyn_cast<CastExpr>(e)) {
      if (c->getCastKind() == clang::CK_ArrayToPointerDecay) {
        return true;
      }
    }
    if (const auto *uo = llvm::dyn_cast<UnaryOperator>(e)) {
      return uo->getOpcode() == clang::UO_AddrOf;
    }
    return false;
  }

  void trackNull(const VarDecl *vd, const Expr *rhs) {
    if (!vd || vd->getType().isNull() || !vd->getType()->isPointerType()) {
      return;
    }
    vd = vd->getCanonicalDecl();
    if (!rhs) {
      st->nulls.erase(vd);
      return;
    }
    if (isNullConst(rhs)) {
      st->nulls[vd] = NullFact::Must;
      return;
    }
    if (obviousNonNull(rhs)) {
      st->nulls[vd] = NullFact::Not;
      return;
    }
    if (const VarDecl *src = plainVar(peel(rhs))) {
      auto it = st->nulls.find(src);
      if (it != st->nulls.end()) {
        st->nulls[vd] = it->second;
        return;
      }
    }
    st->nulls.erase(vd);
  }

  void trackFreed(const VarDecl *vd, const Expr *rhs) {
    if (!vd || vd->getType().isNull() || !vd->getType()->isPointerType()) {
      return;
    }
    vd = vd->getCanonicalDecl();
    const VarDecl *src = rhs ? plainVar(peel(rhs)) : nullptr;
    if (src && st->freed.contains(src->getCanonicalDecl())) {
      st->freed.insert(vd);
    } else {
      st->freed.erase(vd);
    }
  }

  std::optional<FpFact> foldFp(const Expr *e) const {
    if (!e) {
      return std::nullopt;
    }
    const Expr *inner = e->IgnoreParenImpCasts();
    if (!inner || inner->getType().isNull() || !inner->getType()->isRealFloatingType()) {
      return std::nullopt;
    }
    Expr::EvalResult value;
    if (!inner->EvaluateAsRValue(value, ctx) || !value.Val.isFloat()) {
      return std::nullopt;
    }
    const llvm::APFloat &f = value.Val.getFloat();
    if (f.isNaN() || f.isInfinity()) {
      return FpFact::NonFinite;
    }
    return FpFact::Finite;
  }

  void trackFp(const VarDecl *vd, const Expr *rhs) {
    if (!vd || vd->getType().isNull() || !vd->getType()->isRealFloatingType()) {
      return;
    }
    vd = vd->getCanonicalDecl();
    if (auto folded = rhs ? foldFp(rhs) : std::nullopt) {
      st->fps[vd] = *folded;
      return;
    }
    if (const VarDecl *src = rhs ? plainVar(peel(rhs)) : nullptr) {
      auto it = st->fps.find(src);
      if (it != st->fps.end()) {
        st->fps[vd] = it->second;
        return;
      }
    }
    st->fps.erase(vd);
  }

  void noteNull(const Expr *ptr, SourceLocation loc) {
    if (!ptr) {
      return;
    }
    if (isNullConst(ptr)) {
      emitRule(loc, kNull, "dereference of a null pointer", nullptr);
      return;
    }
    const VarDecl *vd = plainVar(peel(ptr));
    if (!vd) {
      return;
    }
    auto it = st->nulls.find(vd);
    if (it != st->nulls.end() && it->second == NullFact::Must) {
      emitRule(loc, kNull, "dereference of a null pointer", vd);
    }
  }

  void noteFreedUse(const VarDecl *vd, SourceLocation loc) {
    if (suppressFreed || !vd || !st->freed.contains(vd)) {
      return;
    }
    emitRule(loc, kFree, "use of a released pointer", vd);
  }

  void noteFpUse(const Expr *e, SourceLocation loc) {
    if (inFiniteTest || !e) {
      return;
    }
    if (auto folded = foldFp(e)) {
      if (*folded == FpFact::NonFinite) {
        emitRule(loc, kFinite, "floating value is not finite", nullptr);
      }
      return;
    }
    const VarDecl *vd = plainVar(peel(e));
    if (!vd) {
      return;
    }
    auto it = st->fps.find(vd);
    if (it != st->fps.end() && it->second == FpFact::NonFinite) {
      emitRule(loc, kFinite, "floating value is not finite", vd);
    }
  }

  bool provenZero(const Expr *e) {
    auto vals = evalInt(e);
    if (vals && vals->size() == 1 && (*vals)[0].isZero()) {
      return true;
    }
    if (!e) {
      return false;
    }
    const Expr *inner = e->IgnoreParenImpCasts();
    if (!inner || inner->getType().isNull() || !inner->getType()->isRealFloatingType()) {
      return false;
    }
    Expr::EvalResult value;
    return inner->EvaluateAsRValue(value, ctx) && value.Val.isFloat() &&
           value.Val.getFloat().isZero();
  }

  std::string calleeName(const clang::CallExpr *call) const {
    const FunctionDecl *fd = call ? call->getDirectCallee() : nullptr;
    if (!fd || !fd->getDeclName().isIdentifier()) {
      return {};
    }
    std::string name = fd->getName().str();
    const std::string prefix = "__builtin_";
    if (name.compare(0, prefix.size(), prefix) == 0) {
      name.erase(0, prefix.size());
    }
    return name;
  }

  bool isReleaseName(const std::string &name) const { return name == "free" || name == "realloc"; }

  bool isFiniteName(const std::string &name) const {
    return name == "isfinite" || name == "isfinitef" || name == "isinf" || name == "isinff" ||
           name == "isnan" || name == "isnanf" || name == "finite";
  }

  std::optional<uint64_t> destBytes(const Expr *e) {
    auto b = boundOf(e);
    if (!b || !b->type) {
      return std::nullopt;
    }
    auto off = norm(b->off);
    if (!off || off->isNegative()) {
      return std::nullopt;
    }
    const uint64_t count = arrayCount(b->type);
    const uint64_t o = off->getZExtValue();
    if (o > count) {
      return std::nullopt;
    }
    const uint64_t elems = count - o;
    const uint64_t width = ctx.getTypeSizeInChars(b->type->getElementType()).getQuantity();
    if (width != 0 && elems > std::numeric_limits<uint64_t>::max() / width) {
      return std::nullopt;
    }
    return elems * width;
  }

  std::optional<uint64_t> litChars(const Expr *e) const {
    e = e ? e->IgnoreParenImpCasts() : nullptr;
    const auto *lit = llvm::dyn_cast_or_null<clang::StringLiteral>(e);
    if (!lit || lit->getCharByteWidth() != 1) {
      return std::nullopt;
    }
    return lit->getLength();
  }

  void noteTooBig(const Expr *dest, const llvm::APSInt &n, SourceLocation loc, bool stringNul) {
    auto cap = destBytes(dest);
    if (!cap || n.isNegative()) {
      return;
    }
    const uint64_t need = n.getZExtValue();
    if (need > *cap) {
      emitRule(loc, kCopy, "copy length exceeds the destination", nullptr);
    }
    if (stringNul && need >= *cap) {
      emitRule(loc, kNul, "string write leaves no room for the terminator", nullptr);
    }
  }

  void noteStringLit(const Expr *dest, const Expr *src, SourceLocation loc) {
    auto chars = litChars(src);
    auto cap = destBytes(dest);
    if (!chars || !cap) {
      return;
    }
    if (*chars > std::numeric_limits<uint64_t>::max() - 1) {
      return;
    }
    if (*chars + 1 > *cap) {
      emitRule(loc, kNul, "string write leaves no room for the terminator", nullptr);
    }
  }

  bool rangesOverlap(uint64_t a, uint64_t an, uint64_t b, uint64_t bn) const {
    if (an == 0 || bn == 0) {
      return false;
    }
    if (a > std::numeric_limits<uint64_t>::max() - an ||
        b > std::numeric_limits<uint64_t>::max() - bn) {
      return true;
    }
    return a < b + bn && b < a + an;
  }

  std::optional<uint64_t> elemBytes(const ArrBound &b) const {
    if (!b.type) {
      return std::nullopt;
    }
    auto off = norm(b.off);
    if (!off || off->isNegative()) {
      return std::nullopt;
    }
    const uint64_t width = ctx.getTypeSizeInChars(b.type->getElementType()).getQuantity();
    const uint64_t o = off->getZExtValue();
    if (width != 0 && o > std::numeric_limits<uint64_t>::max() / width) {
      return std::nullopt;
    }
    return o * width;
  }

  void noteOverlap(const clang::CallExpr *call, const std::string &name) {
    if (name != "memcpy" && name != "mempcpy") {
      return;
    }
    if (call->getNumArgs() < 3) {
      return;
    }
    const Expr *dest = call->getArg(0);
    const Expr *src = call->getArg(1);
    auto n = evalInt(call->getArg(2));
    if (n && n->size() == 1 && (*n)[0].isZero()) {
      return;
    }
    auto bd = boundOf(dest);
    auto bs = boundOf(src);
    if (bd && bs && bd->base && bd->base == bs->base && n && n->size() == 1 &&
        !(*n)[0].isNegative()) {
      auto d0 = elemBytes(*bd);
      auto s0 = elemBytes(*bs);
      if (d0 && s0 && rangesOverlap(*d0, (*n)[0].getZExtValue(), *s0, (*n)[0].getZExtValue())) {
        emitRule(call->getBeginLoc(), kOverlap, "copy overlaps itself", bd->base);
        return;
      }
    }
    const VarDecl *ds = plainVar(peel(dest));
    const VarDecl *ss = plainVar(peel(src));
    if (ds && ds == ss) {
      emitRule(call->getBeginLoc(), kOverlap, "copy overlaps itself", ds);
    }
  }

  void noteCopy(const clang::CallExpr *call, const std::string &name) {
    const SourceLocation loc = call->getBeginLoc();
    if ((name == "memcpy" || name == "memmove" || name == "memset" || name == "mempcpy") &&
        call->getNumArgs() >= 3) {
      auto n = evalInt(call->getArg(name == "memset" ? 2 : 2));
      if (n && n->size() == 1) {
        noteTooBig(call->getArg(0), (*n)[0], loc, false);
      }
      return;
    }
    if (name == "strncpy" && call->getNumArgs() >= 3) {
      auto n = evalInt(call->getArg(2));
      if (n && n->size() == 1) {
        noteTooBig(call->getArg(0), (*n)[0], loc, true);
      }
      return;
    }
    if (name == "strncat" && call->getNumArgs() >= 3) {
      auto n = evalInt(call->getArg(2));
      auto cap = destBytes(call->getArg(0));
      if (n && n->size() == 1 && !(*n)[0].isNegative() && cap) {
        const uint64_t extra = (*n)[0].getZExtValue();
        if (extra < std::numeric_limits<uint64_t>::max() && extra + 1 > *cap) {
          emitRule(loc, kNul, "string write leaves no room for the terminator", nullptr);
        }
      }
      return;
    }
    if ((name == "snprintf" || name == "vsnprintf") && call->getNumArgs() >= 2) {
      auto n = evalInt(call->getArg(1));
      if (n && n->size() == 1) {
        noteTooBig(call->getArg(0), (*n)[0], loc, false);
      }
      return;
    }
    if (name == "strcpy" || name == "stpcpy" || name == "strcat" || name == "sprintf") {
      const unsigned srcSlot = name == "sprintf" ? 1 : 1;
      if (call->getNumArgs() > srcSlot) {
        noteStringLit(call->getArg(0), call->getArg(srcSlot), loc);
      }
    }
  }

  void noteCall(const clang::CallExpr *call) {
    const std::string name = calleeName(call);
    if (name.empty()) {
      return;
    }
    noteOverlap(call, name);
    noteCopy(call, name);
  }

  void forget(const VarDecl *vd) {
    if (!vd) {
      return;
    }
    vd = vd->getCanonicalDecl();
    st->ptrs.erase(vd);
    st->ints.erase(vd);
    st->arrs.erase(vd);
    st->nulls.erase(vd);
    st->fps.erase(vd);
    st->freed.erase(vd);
  }

  // A call may write through a pointer argument. &local escapes, so the local is no longer proven.
  void forgetEscaped(const clang::CallExpr *call) {
    if (!call) {
      return;
    }
    for (const Expr *arg : call->arguments()) {
      const Expr *e = peel(arg);
      const auto *uo = llvm::dyn_cast_or_null<UnaryOperator>(e);
      if (!uo || uo->getOpcode() != clang::UO_AddrOf) {
        continue;
      }
      forget(plainVar(uo->getSubExpr()));
    }
  }

  void markReleased(const Expr *arg, bool emit, SourceLocation loc) {
    const VarDecl *vd = plainVar(peel(arg));
    if (!vd) {
      return;
    }
    vd = vd->getCanonicalDecl();
    if (emit && st->freed.contains(vd)) {
      emitRule(loc, kFree, "use of a released pointer", vd);
    }
    st->freed.insert(vd);
  }

  struct ObjKey {
    const ValueDecl *owner = nullptr;
    const ValueDecl *decl = nullptr;
    bool operator==(const ObjKey &o) const { return owner == o.owner && decl == o.decl; }
  };

  std::optional<ObjKey> objectOf(const Expr *e) {
    if (!e) {
      return std::nullopt;
    }
    if (auto b = boundOf(e)) {
      if (b->base) {
        return ObjKey{b->base, b->base};
      }
    }
    e = peel(e);
    if (const auto *uo = llvm::dyn_cast_or_null<UnaryOperator>(e)) {
      if (uo->getOpcode() == clang::UO_AddrOf) {
        const Expr *sub = uo->getSubExpr()->IgnoreParenImpCasts();
        if (const VarDecl *vd = plainVar(sub)) {
          return ObjKey{vd, vd};
        }
        if (const auto *me = llvm::dyn_cast<MemberExpr>(sub)) {
          const VarDecl *owner = plainVar(me->getBase());
          const auto *field = llvm::dyn_cast<ValueDecl>(me->getMemberDecl());
          if (owner && field) {
            return ObjKey{owner->getCanonicalDecl(),
                          llvm::cast<ValueDecl>(field->getCanonicalDecl())};
          }
        }
      }
    }
    return std::nullopt;
  }

  void noteSame(const BinaryOperator *bo) {
    const auto op = bo->getOpcode();
    const bool rel = bo->isRelationalOp();
    const bool sub = op == clang::BO_Sub;
    if (!rel && !sub) {
      return;
    }
    const Expr *lhs = bo->getLHS();
    const Expr *rhs = bo->getRHS();
    if (!lhs->getType()->isPointerType() || !rhs->getType()->isPointerType()) {
      return;
    }
    auto L = objectOf(lhs);
    auto R = objectOf(rhs);
    if (!L || !R || *L == *R) {
      return;
    }
    emitRule(
        bo->getOperatorLoc(), kSame, "pointer compare or subtraction leaves this array", L->decl);
  }

  void noteAssignOverlap(const BinaryOperator *bo) {
    if (bo->getOpcode() != clang::BO_Assign) {
      return;
    }
    const Expr *lhs = bo->getLHS()->IgnoreParenImpCasts();
    const Expr *rhs = bo->getRHS()->IgnoreParenImpCasts();
    QualType t = lhs->getType();
    if (t.isNull() || t->isScalarType()) {
      return;
    }
    const VarDecl *ld = plainVar(lhs);
    const VarDecl *rd = plainVar(rhs);
    if (ld && ld == rd) {
      emitRule(bo->getOperatorLoc(), kOverlap, "copy overlaps itself", ld);
    }
  }

  bool onePast(const llvm::APSInt &off, const llvm::APSInt &idx, uint64_t size) const {
    auto o = norm(off);
    auto n = norm(idx);
    if (!o || !n || o->isNegative() || n->isNegative()) {
      return false;
    }
    const uint64_t a = o->getZExtValue();
    const uint64_t b = n->getZExtValue();
    if (a > std::numeric_limits<uint64_t>::max() - b) {
      return false;
    }
    return a + b == size;
  }

  void checkVals(const ArrBound &b, const IntList &vals, SourceLocation loc) {
    if (!b.type) {
      return;
    }
    const uint64_t size = arrayCount(b.type);
    for (const auto &v : vals) {
      if (onePast(b.off, v, size)) {
        emitRule(loc, kOnePast, "dereference of a one-past-the-end pointer", b.base);
      }
      if (oob(b.off, v, size)) {
        flagBounds(b.base, loc);
        return;
      }
    }
  }

  void checkSub(const ArraySubscriptExpr *sub) {
    auto b = boundOf(sub->getBase());
    if (!b) {
      return;
    }
    auto vals = evalInt(sub->getIdx());
    if (!vals) {
      return;
    }
    SourceLocation at = sub->getIdx()->getExprLoc();
    if (at.isInvalid()) {
      at = sub->getBeginLoc();
    }
    checkVals(*b, *vals, at);
  }

  void checkDeref(const UnaryOperator *uo) {
    auto b = boundOf(uo->getSubExpr());
    if (!b) {
      return;
    }
    IntList zero;
    zero.push_back(i64(0));
    checkVals(*b, zero, uo->getOperatorLoc());
  }

  void scan(const Expr *e) {
    if (!e) {
      return;
    }
    if (const auto *call = llvm::dyn_cast<clang::CallExpr>(e)) {
      const std::string name = calleeName(call);
      noteCall(call);
      const bool finite = isFiniteName(name);
      const bool release = isReleaseName(name);
      const bool prevFinite = inFiniteTest;
      const bool prevFreed = suppressFreed;
      if (finite) {
        inFiniteTest = true;
      }
      unsigned index = 0;
      for (const Expr *arg : call->arguments()) {
        if (release && index == 0) {
          suppressFreed = true;
        }
        scan(arg);
        suppressFreed = prevFreed;
        ++index;
      }
      inFiniteTest = prevFinite;
      if (!finite) {
        scan(call->getCallee());
      }
      return;
    }
    if (const auto *sub = llvm::dyn_cast<ArraySubscriptExpr>(e)) {
      checkSub(sub);
      noteNull(sub->getBase(), sub->getBeginLoc());
    } else if (const auto *uo = llvm::dyn_cast<UnaryOperator>(e)) {
      if (uo->getOpcode() == clang::UO_Deref) {
        checkDeref(uo);
        noteNull(uo->getSubExpr(), uo->getOperatorLoc());
      } else if (uo->getOpcode() == clang::UO_Minus) {
        noteSigned(uo, uo->getOperatorLoc());
      }
    } else if (const auto *me = llvm::dyn_cast<MemberExpr>(e)) {
      if (me->isArrow()) {
        noteNull(me->getBase(), me->getOperatorLoc());
      }
    } else if (const auto *bo = llvm::dyn_cast<BinaryOperator>(e)) {
      const auto op = bo->getOpcode();
      if (op == clang::BO_Div || op == clang::BO_Rem || op == clang::BO_DivAssign ||
          op == clang::BO_RemAssign) {
        if (provenZero(bo->getRHS())) {
          const VarDecl *vd = plainVar(peel(bo->getRHS()));
          emitRule(bo->getOperatorLoc(), kDiv, "divisor is zero", vd);
        }
      }
      if (op == clang::BO_Add || op == clang::BO_Sub || op == clang::BO_Mul ||
          op == clang::BO_Div || op == clang::BO_Rem || op == clang::BO_Shl) {
        noteSigned(bo, bo->getOperatorLoc());
      }
      noteSame(bo);
      if (!bo->getType().isNull() && bo->getType()->isRealFloatingType()) {
        noteFpUse(bo, bo->getExprLoc());
      }
    } else if (const VarDecl *vd = plainVar(e)) {
      noteFreedUse(vd, e->getExprLoc());
      noteFpUse(e, e->getExprLoc());
    } else {
      noteFpUse(e, e->getExprLoc());
    }
    for (const Stmt *c : e->children()) {
      if (const auto *ce = llvm::dyn_cast_or_null<Expr>(c)) {
        scan(ce);
      }
    }
  }

  bool baseOutlives(const Expr *base, const VarList &held) const {
    if (!base) {
      return true;
    }
    base = base->IgnoreParenImpCasts();
    if (llvm::isa<CXXThisExpr>(base)) {
      return true;
    }
    if (const VarDecl *vd = plainVar(base)) {
      return slotOutlives(vd, held);
    }
    return true;
  }

  void flagEscaping(const Expr *lhs, const Expr *rhs, SourceLocation loc) {
    VarList held = autosOf(rhs);
    if (held.empty()) {
      return;
    }
    lhs = lhs->IgnoreParenImpCasts();
    if (const auto *uo = llvm::dyn_cast<UnaryOperator>(lhs)) {
      if (uo->getOpcode() == clang::UO_Deref) {
        flagDangling(held, loc);
        return;
      }
    }
    if (const auto *me = llvm::dyn_cast<MemberExpr>(lhs)) {
      if (baseOutlives(me->getBase(), held)) {
        flagDangling(held, loc);
      }
      return;
    }
    if (const auto *as = llvm::dyn_cast<ArraySubscriptExpr>(lhs)) {
      if (baseOutlives(as->getBase(), held)) {
        flagDangling(held, loc);
      }
    }
  }

  void trackPointer(const VarDecl *vd, const Expr *rhs, bool bindRef) {
    VarList held;
    if (rhs) {
      held = bindRef ? objectAuto(rhs) : autosOf(rhs);
    }
    st->ptrs[vd] = held;
    if (!bindRef) {
      auto b = rhs ? boundOf(rhs) : std::nullopt;
      if (b && b->base) {
        st->arrs[vd] = ArrRef{b->base, b->off};
      } else {
        st->arrs.erase(vd);
      }
    }
  }

  void trackWrite(const VarDecl *vd, const Expr *rhs, bool emit, SourceLocation loc, bool init) {
    vd = vd->getCanonicalDecl();
    if (vd->getType()->isReferenceType()) {
      if (!init) {
        return;
      }
      VarList held = rhs ? objectAuto(rhs) : VarList{};
      if (vd->hasLocalStorage()) {
        st->ptrs[vd] = held;
      }
      if (emit && !vd->isImplicit() && !held.empty() && slotOutlives(vd, held)) {
        flagDangling(held, loc);
      }
      return;
    }
    if (integral(vd) && (vd->hasLocalStorage() || llvm::isa<ParmVarDecl>(vd))) {
      auto vals = rhs ? evalInt(rhs) : std::nullopt;
      if (!vals) {
        st->ints.erase(vd);
      } else {
        st->ints[vd] = *vals;
      }
    }
    if (vd->getType()->isRealFloatingType()) {
      trackFp(vd, rhs);
    }
    if (!vd->getType()->isPointerType()) {
      return;
    }
    trackNull(vd, rhs);
    trackFreed(vd, rhs);
    VarList held = rhs ? autosOf(rhs) : VarList{};
    if (vd->hasLocalStorage()) {
      trackPointer(vd, rhs, false);
    }
    if (emit && !vd->isImplicit() && !held.empty() && slotOutlives(vd, held)) {
      flagDangling(held, loc);
    }
  }

  void applyInc(const UnaryOperator *uo, bool emit) {
    const VarDecl *vd = plainVar(uo->getSubExpr());
    if (!vd) {
      return;
    }
    const bool dec = uo->isDecrementOp();
    auto it = st->ints.find(vd);
    if (it != st->ints.end()) {
      IntList next;
      bool ok = true;
      for (const auto &v : it->second) {
        if (!addInt(next, dec ? (v - i64(1)) : (v + i64(1)))) {
          ok = false;
          break;
        }
      }
      if (!ok) {
        st->ints.erase(vd);
      } else {
        st->ints[vd] = next;
      }
      if (emit) {
        noteStored(vd, uo->getOperatorLoc());
      }
    }
    auto ar = st->arrs.find(vd);
    if (ar != st->arrs.end()) {
      auto n = norm(dec ? (ar->second.off - i64(1)) : (ar->second.off + i64(1)));
      if (!n) {
        st->arrs.erase(vd);
      } else {
        ar->second.off = *n;
      }
    }
  }

  void applyCompound(const BinaryOperator *bo, bool emit) {
    const VarDecl *vd = plainVar(bo->getLHS());
    if (!vd) {
      return;
    }
    auto op = bo->getOpcode();
    if (integral(vd)) {
      auto base = evalInt(bo->getLHS());
      auto delta = evalInt(bo->getRHS());
      if (!base || !delta || base->size() * delta->size() > 4) {
        st->ints.erase(vd);
        return;
      }
      IntList next;
      bool ok = true;
      for (const auto &a : *base) {
        for (const auto &b : *delta) {
          llvm::APSInt v = a;
          if (op == clang::BO_AddAssign) {
            v = a + b;
          } else if (op == clang::BO_SubAssign) {
            v = a - b;
          } else {
            ok = false;
            break;
          }
          if (!addInt(next, std::move(v))) {
            ok = false;
            break;
          }
        }
      }
      if (!ok) {
        st->ints.erase(vd);
      } else {
        st->ints[vd] = next;
      }
      if (emit) {
        noteStored(vd, bo->getOperatorLoc());
      }
      return;
    }
    if (vd->getType()->isPointerType() &&
        (op == clang::BO_AddAssign || op == clang::BO_SubAssign)) {
      auto ar = st->arrs.find(vd);
      auto delta = evalInt(bo->getRHS());
      if (ar == st->arrs.end() || !delta || delta->size() != 1) {
        st->arrs.erase(vd);
        return;
      }
      auto n = norm(op == clang::BO_SubAssign ? (ar->second.off - (*delta)[0])
                                              : (ar->second.off + (*delta)[0]));
      if (!n) {
        st->arrs.erase(vd);
      } else {
        ar->second.off = *n;
      }
    }
  }

  void transfer(const Stmt *s, bool emit) {
    s = peelStmt(s);
    if (!s || llvm::isa<CompoundStmt>(s) || llvm::isa<IfStmt>(s) || llvm::isa<WhileStmt>(s) ||
        llvm::isa<ForStmt>(s) || llvm::isa<DoStmt>(s) || llvm::isa<SwitchStmt>(s) ||
        llvm::isa<CXXForRangeStmt>(s)) {
      return;
    }
    if (const auto *ds = llvm::dyn_cast<DeclStmt>(s)) {
      for (const Decl *d : ds->decls()) {
        const auto *vd = llvm::dyn_cast<VarDecl>(d);
        if (!vd) {
          continue;
        }
        if (emit && vd->getInit()) {
          scan(vd->getInit());
        }
        trackWrite(vd, vd->getInit(), emit, vd->getLocation(), true);
      }
      return;
    }
    if (const auto *rs = llvm::dyn_cast<ReturnStmt>(s)) {
      if (const Expr *e = rs->getRetValue()) {
        if (emit) {
          scan(e);
          flagDangling(autosOf(e), rs->getReturnLoc());
        }
      }
      return;
    }
    if (const auto *bo = llvm::dyn_cast<BinaryOperator>(s)) {
      if (bo->isCompoundAssignmentOp()) {
        if (emit) {
          scan(bo->getLHS());
          scan(bo->getRHS());
        }
        applyCompound(bo, emit);
        return;
      }
      if (bo->isAssignmentOp()) {
        if (emit) {
          scan(bo->getRHS());
          if (!plainVar(bo->getLHS())) {
            scan(bo->getLHS());
          }
          noteAssignOverlap(bo);
        }
        if (const VarDecl *vd = plainVar(bo->getLHS())) {
          trackWrite(vd, bo->getRHS(), emit, bo->getOperatorLoc(), false);
        } else if (emit) {
          flagEscaping(bo->getLHS(), bo->getRHS(), bo->getOperatorLoc());
        }
        return;
      }
    }
    if (const auto *uo = llvm::dyn_cast<UnaryOperator>(s)) {
      if (uo->isIncrementDecrementOp()) {
        if (emit) {
          scan(uo->getSubExpr());
        }
        applyInc(uo, emit);
        return;
      }
    }
    if (const auto *call = llvm::dyn_cast<clang::CallExpr>(s)) {
      if (emit) {
        scan(call);
      }
      if (isReleaseName(calleeName(call)) && call->getNumArgs() >= 1) {
        markReleased(call->getArg(0), emit, call->getBeginLoc());
      }
      forgetEscaped(call);
      return;
    }
    if (const auto *del = llvm::dyn_cast<clang::CXXDeleteExpr>(s)) {
      if (emit) {
        const bool prev = suppressFreed;
        suppressFreed = true;
        scan(del->getArgument());
        suppressFreed = prev;
      }
      markReleased(del->getArgument(), emit, del->getBeginLoc());
      return;
    }
    if (emit) {
      if (const auto *e = llvm::dyn_cast<Expr>(s)) {
        scan(e);
      }
    }
  }

  const Expr *termExpr(const Stmt *term) const {
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
    if (const auto *sw = llvm::dyn_cast<SwitchStmt>(term)) {
      return sw->getCond();
    }
    if (const auto *c = llvm::dyn_cast<ConditionalOperator>(term)) {
      return c->getCond();
    }
    if (const auto *e = llvm::dyn_cast<Expr>(term)) {
      return e;
    }
    return nullptr;
  }

  void setZero(State &s, const VarDecl *vd) const {
    IntList one;
    if (addInt(one, i64(0))) {
      s.ints[vd] = std::move(one);
    }
  }

  void dropZero(State &s, const VarDecl *vd) const {
    auto it = s.ints.find(vd);
    if (it == s.ints.end()) {
      return;
    }
    IntList keep;
    for (const auto &v : it->second) {
      if (!v.isZero()) {
        keep.push_back(v);
      }
    }
    if (keep.empty()) {
      s.ints.erase(vd);
    } else {
      s.ints[vd] = std::move(keep);
    }
  }

  void pinEqual(State &s, const Expr *varSide, const Expr *other, bool equal) const {
    const VarDecl *vd = plainVar(varSide ? varSide->IgnoreParenImpCasts() : nullptr);
    if (!vd) {
      return;
    }
    if (vd->getType()->isPointerType()) {
      if (!isNullConst(other)) {
        return;
      }
      s.nulls[vd] = equal ? NullFact::Must : NullFact::Not;
      return;
    }
    if (!integral(vd) || !other) {
      return;
    }
    Expr::EvalResult value;
    if (!other->IgnoreParenImpCasts()->EvaluateAsInt(value, ctx) || !value.Val.isInt()) {
      return;
    }
    if (equal) {
      IntList one;
      if (addInt(one, value.Val.getInt())) {
        s.ints[vd] = std::move(one);
      }
      return;
    }
    auto it = s.ints.find(vd);
    if (it == s.ints.end()) {
      return;
    }
    auto banned = norm(value.Val.getInt());
    IntList keep;
    for (const auto &v : it->second) {
      if (!banned || v != *banned) {
        keep.push_back(v);
      }
    }
    if (keep.empty()) {
      s.ints.erase(vd);
    } else {
      s.ints[vd] = std::move(keep);
    }
  }

  void applyCond(State &s, const Expr *cond, bool whenTrue) const {
    if (!cond) {
      return;
    }
    cond = cond->IgnoreParenImpCasts();
    if (const auto *uo = llvm::dyn_cast<UnaryOperator>(cond)) {
      if (uo->getOpcode() == clang::UO_LNot) {
        applyCond(s, uo->getSubExpr(), !whenTrue);
      }
      return;
    }
    if (const auto *bo = llvm::dyn_cast<BinaryOperator>(cond)) {
      const auto op = bo->getOpcode();
      if (op == clang::BO_LAnd) {
        if (whenTrue) {
          applyCond(s, bo->getLHS(), true);
          applyCond(s, bo->getRHS(), true);
        }
        return;
      }
      if (op == clang::BO_LOr) {
        if (!whenTrue) {
          applyCond(s, bo->getLHS(), false);
          applyCond(s, bo->getRHS(), false);
        }
        return;
      }
      if (op == clang::BO_EQ || op == clang::BO_NE) {
        const bool equal = (op == clang::BO_EQ) == whenTrue;
        pinEqual(s, bo->getLHS(), bo->getRHS(), equal);
        pinEqual(s, bo->getRHS(), bo->getLHS(), equal);
        return;
      }
    }
    if (const VarDecl *vd = plainVar(cond)) {
      if (vd->getType()->isPointerType()) {
        s.nulls[vd] = whenTrue ? NullFact::Not : NullFact::Must;
      } else if (integral(vd)) {
        if (whenTrue) {
          dropZero(s, vd);
        } else {
          setZero(s, vd);
        }
      }
    }
  }

  void refineEdge(State &s, const CFGBlock *from, const CFGBlock *to) const {
    if (!from || !to) {
      return;
    }
    const Stmt *term = from->getTerminatorStmt();
    if (!llvm::isa_and_nonnull<IfStmt>(term) && !llvm::isa_and_nonnull<WhileStmt>(term) &&
        !llvm::isa_and_nonnull<DoStmt>(term) && !llvm::isa_and_nonnull<ForStmt>(term) &&
        !llvm::isa_and_nonnull<ConditionalOperator>(term)) {
      return;
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
      return;
    }
    applyCond(s, termExpr(term), index == 0);
  }

  State join(const CFGBlock *b, bool &partial) const {
    partial = false;
    std::vector<const CFGBlock *> blocks;
    for (const CFGBlock *p : b->preds()) {
      if (!p || !reach[p->getBlockID()]) {
        continue;
      }
      if (!ready[p->getBlockID()]) {
        partial = true;
        continue;
      }
      blocks.push_back(p);
    }
    std::vector<State> preds;
    preds.reserve(blocks.size());
    for (const CFGBlock *p : blocks) {
      preds.push_back(outs[p->getBlockID()]);
      if (!partial) {
        refineEdge(preds.back(), p, b);
      }
    }
    State out;
    for (const State &p : preds) {
      for (const auto &kv : p.ptrs) {
        addVars(out.ptrs[kv.first], kv.second);
      }
    }
    if (partial || preds.empty()) {
      return out;
    }
    for (const auto &kv : preds[0].ints) {
      IntList acc = kv.second;
      bool all = true;
      for (size_t i = 1; i < preds.size(); ++i) {
        auto it = preds[i].ints.find(kv.first);
        if (it == preds[i].ints.end()) {
          all = false;
          break;
        }
        for (const auto &v : it->second) {
          if (!addInt(acc, v)) {
            all = false;
            break;
          }
        }
        if (!all) {
          break;
        }
      }
      if (all) {
        out.ints[kv.first] = std::move(acc);
      }
    }
    for (const auto &kv : preds[0].arrs) {
      bool all = true;
      for (size_t i = 1; i < preds.size(); ++i) {
        auto it = preds[i].arrs.find(kv.first);
        if (it == preds[i].arrs.end() || !(it->second == kv.second)) {
          all = false;
          break;
        }
      }
      if (all) {
        out.arrs[kv.first] = kv.second;
      }
    }
    for (const auto &kv : preds[0].nulls) {
      bool all = true;
      for (size_t i = 1; i < preds.size(); ++i) {
        auto it = preds[i].nulls.find(kv.first);
        if (it == preds[i].nulls.end() || it->second != kv.second) {
          all = false;
          break;
        }
      }
      if (all) {
        out.nulls[kv.first] = kv.second;
      }
    }
    for (const auto &kv : preds[0].fps) {
      bool all = true;
      for (size_t i = 1; i < preds.size(); ++i) {
        auto it = preds[i].fps.find(kv.first);
        if (it == preds[i].fps.end() || it->second != kv.second) {
          all = false;
          break;
        }
      }
      if (all) {
        out.fps[kv.first] = kv.second;
      }
    }
    for (const VarDecl *vd : preds[0].freed) {
      bool all = true;
      for (size_t i = 1; i < preds.size(); ++i) {
        if (!preds[i].freed.contains(vd)) {
          all = false;
          break;
        }
      }
      if (all) {
        out.freed.insert(vd);
      }
    }
    return out;
  }
};

bool systemBody(const FunctionDecl *fn, const SourceManager &sm) {
  if (!fn->getBody()) {
    return false;
  }
  SourceLocation loc = sm.getSpellingLoc(fn->getBody()->getBeginLoc());
  return loc.isValid() && sm.isInSystemHeader(loc);
}

class Pass : public clang::ast_matchers::MatchFinder::MatchCallback {
public:
  explicit Pass(Reporter &reporter) : reporter(reporter) {}

  void run(const clang::ast_matchers::MatchFinder::MatchResult &result) override {
    const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn");
    if (!fn || !fn->hasBody() || !fn->getBody() || !result.Context || !result.SourceManager) {
      return;
    }
    if (fn->isInvalidDecl()) {
      return;
    }
    const SourceManager &sm = *result.SourceManager;
    if (systemBody(fn, sm)) {
      return;
    }
    checkLoops(fn, *result.Context, reporter, sm);
    checkInvariant(fn, *result.Context, reporter, sm);
    checkUnseq(fn, reporter, sm);
    if (fn->isDependentContext()) {
      return;
    }
    AnalysisDeclContextManager mgr(*result.Context);
    // Subexpressions must be CFG elements or Clang's uninit visitor never sees a load.
    mgr.getCFGBuildOptions().setAllAlwaysAdd();
    AnalysisDeclContext *ac = mgr.getContext(fn);
    if (!ac) {
      return;
    }
    CFG *cfg = ac->getCFG();
    if (!cfg) {
      return;
    }
    checkUninit(*fn, *cfg, *ac, reporter, sm);
    Lattice lattice(fn, *result.Context, reporter, sm);
    lattice.run(*cfg);
  }

private:
  Reporter &reporter;
};

} // namespace

void DataflowCheck::registerMatchers(clang::ast_matchers::MatchFinder &) {}

void DataflowCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &) {}

void attachDataflowPass(clang::ast_matchers::MatchFinder &finder,
                        Reporter &reporter,
                        std::unique_ptr<clang::ast_matchers::MatchFinder::MatchCallback> &slot) {
  slot = std::make_unique<Pass>(reporter);
  finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("fn"), slot.get());
}
