#include "siliscope/MinScope.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/Linkage.h"
#include "clang/Basic/SourceManager.h"

#include <map>
#include <set>
#include <string>
#include <vector>

using clang::CompoundStmt;
using clang::DeclRefExpr;
using clang::DeclStmt;
using clang::Expr;
using clang::FunctionDecl;
using clang::TranslationUnitDecl;
using clang::VarDecl;
using clang::ast_matchers::translationUnitDecl;

namespace {

constexpr const char *kId = "ss.decl.min-scope";

bool inMain(const clang::Decl *d, const clang::SourceManager &sm) {
  const clang::SourceLocation loc = d->getLocation();
  return loc.isValid() && sm.isWrittenInMainFile(sm.getSpellingLoc(loc));
}

bool allowed(const clang::NamedDecl *d, Reporter &reporter) {
  if (!d || d->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = d->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

bool trivialInit(const Expr *init) {
  if (!init) {
    return true;
  }
  init = init->IgnoreParenImpCasts();
  return llvm::isa<clang::IntegerLiteral>(init) || llvm::isa<clang::FloatingLiteral>(init) ||
         llvm::isa<clang::CharacterLiteral>(init) || llvm::isa<clang::StringLiteral>(init) ||
         llvm::isa<clang::CXXBoolLiteralExpr>(init) ||
         llvm::isa<clang::CXXNullPtrLiteralExpr>(init) || llvm::isa<clang::GNUNullExpr>(init);
}

bool hits(clang::DynTypedNode node, clang::ASTContext &ctx, const clang::Stmt *target) {
  for (int depth = 0; depth < 64; ++depth) {
    if (node.get<clang::Stmt>() == target) {
      return true;
    }
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      return false;
    }
    node = parents[0];
  }
  return false;
}

const CompoundStmt *innermostCompound(const clang::Stmt *s, clang::ASTContext &ctx) {
  clang::DynTypedNode node = clang::DynTypedNode::create(*s);
  for (int depth = 0; depth < 64; ++depth) {
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      return nullptr;
    }
    node = parents[0];
    if (const auto *compound = node.get<CompoundStmt>()) {
      return compound;
    }
  }
  return nullptr;
}

const CompoundStmt *homeCompound(const VarDecl *vd, clang::ASTContext &ctx) {
  clang::DynTypedNode node = clang::DynTypedNode::create(*vd);
  for (int depth = 0; depth < 8; ++depth) {
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      return nullptr;
    }
    node = parents[0];
    if (node.get<DeclStmt>()) {
      continue;
    }
    if (const auto *compound = node.get<CompoundStmt>()) {
      return compound;
    }
    // A declaration in a for/if/while already sits at the statement it serves.
    if (node.get<clang::ForStmt>() || node.get<clang::WhileStmt>() || node.get<clang::IfStmt>() ||
        node.get<clang::SwitchStmt>() || node.get<clang::CXXForRangeStmt>() ||
        node.get<clang::DoStmt>()) {
      return nullptr;
    }
    return nullptr;
  }
  return nullptr;
}

bool insideLambda(const Expr *e, clang::ASTContext &ctx, const CompoundStmt *home) {
  clang::DynTypedNode node = clang::DynTypedNode::create(*e);
  for (int depth = 0; depth < 64; ++depth) {
    if (node.get<CompoundStmt>() == home) {
      return false;
    }
    if (node.get<clang::LambdaExpr>()) {
      return true;
    }
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      return false;
    }
    node = parents[0];
  }
  return false;
}

const FunctionDecl *enclosingFn(const Expr *e, clang::ASTContext &ctx, bool &outside) {
  clang::DynTypedNode node = clang::DynTypedNode::create(*e);
  for (int depth = 0; depth < 64; ++depth) {
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      outside = true;
      return nullptr;
    }
    node = parents[0];
    if (const auto *fn = node.get<FunctionDecl>()) {
      return fn;
    }
    if (node.get<TranslationUnitDecl>()) {
      outside = true;
      return nullptr;
    }
  }
  outside = true;
  return nullptr;
}

struct Uses {
  std::set<const FunctionDecl *> fns;
  std::vector<const DeclRefExpr *> refs;
  bool outside = false;
};

class Collector : public clang::RecursiveASTVisitor<Collector> {
public:
  explicit Collector(clang::ASTContext &ctx) : ctx(ctx) {}

  bool VisitDeclRefExpr(DeclRefExpr *dre) {
    if (!dre) {
      return true;
    }
    const auto *vd = llvm::dyn_cast<VarDecl>(dre->getDecl());
    if (!vd) {
      return true;
    }
    Uses &slot = uses[vd->getCanonicalDecl()];
    slot.refs.push_back(dre);
    bool outside = false;
    if (const FunctionDecl *fn = enclosingFn(dre, ctx, outside)) {
      slot.fns.insert(fn->getCanonicalDecl());
    }
    if (outside) {
      slot.outside = true;
    }
    return true;
  }

  clang::ASTContext &ctx;
  std::map<const VarDecl *, Uses> uses;
};

const CompoundStmt *tighter(const CompoundStmt *home,
                            const std::vector<const DeclRefExpr *> &refs,
                            clang::ASTContext &ctx) {
  const CompoundStmt *cursor = innermostCompound(refs.front(), ctx);
  while (cursor && cursor != home) {
    bool all = true;
    for (const DeclRefExpr *ref : refs) {
      if (!hits(clang::DynTypedNode::create(*ref), ctx, cursor)) {
        all = false;
        break;
      }
    }
    if (all) {
      return cursor;
    }
    cursor = innermostCompound(cursor, ctx);
  }
  return nullptr;
}

} // namespace

void MinScopeCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void MinScopeCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *tu = result.Nodes.getNodeAs<TranslationUnitDecl>("tu");
  if (!tu || !result.Context || !result.SourceManager) {
    return;
  }
  clang::ASTContext &ctx = *result.Context;
  const clang::SourceManager &sm = *result.SourceManager;
  Collector walk(ctx);
  walk.TraverseDecl(const_cast<TranslationUnitDecl *>(tu));

  class Locals : public clang::RecursiveASTVisitor<Locals> {
  public:
    const clang::SourceManager *sm = nullptr;
    std::vector<const VarDecl *> vars;
    bool VisitVarDecl(VarDecl *vd) {
      if (!vd || vd->isImplicit() || llvm::isa<clang::ParmVarDecl>(vd) || !vd->hasLocalStorage() ||
          vd->isStaticLocal() || !inMain(vd, *sm)) {
        return true;
      }
      vars.push_back(vd);
      return true;
    }
  } locals;
  locals.sm = &sm;
  locals.TraverseDecl(const_cast<TranslationUnitDecl *>(tu));

  for (const auto &entry : walk.uses) {
    const VarDecl *vd = entry.first;
    const Uses &slot = entry.second;
    if (!vd->isFileVarDecl() || vd->getFormalLinkage() != clang::Linkage::Internal ||
        !inMain(vd, sm) || slot.outside || slot.fns.size() != 1 || !vd->isReferenced() ||
        allowed(vd, reporter)) {
      continue;
    }
    reporter.emit(sm, vd->getLocation(), kId, "declare this object in the function that uses it");
  }

  for (const VarDecl *vd : locals.vars) {
    if (!trivialInit(vd->getInit()) || allowed(vd, reporter)) {
      continue;
    }
    const CompoundStmt *home = homeCompound(vd, ctx);
    if (!home) {
      continue;
    }
    const auto found = walk.uses.find(vd->getCanonicalDecl());
    if (found == walk.uses.end() || found->second.refs.empty()) {
      continue;
    }
    bool captured = false;
    for (const DeclRefExpr *ref : found->second.refs) {
      if (insideLambda(ref, ctx, home)) {
        captured = true;
        break;
      }
    }
    if (captured || !tighter(home, found->second.refs, ctx)) {
      continue;
    }
    reporter.emit(sm, vd->getLocation(), kId, "declare this where it is used");
  }
}
