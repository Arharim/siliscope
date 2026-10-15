#include "siliscope/NoDataRace.h"

#include "siliscope/Isr.h"
#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <map>
#include <set>
#include <string>
#include <vector>

using clang::BinaryOperator;
using clang::DeclRefExpr;
using clang::Expr;
using clang::FunctionDecl;
using clang::QualType;
using clang::SourceLocation;
using clang::SourceManager;
using clang::TranslationUnitDecl;
using clang::UnaryOperator;
using clang::VarDecl;
using clang::ast_matchers::translationUnitDecl;

namespace {

constexpr const char *kId = "ss.conc.no-data-race";
constexpr const char *kMsg = "say how this shared object stays consistent";

enum class Role { Isr, Thread, Main, Plain };

bool allowed(const VarDecl *var, Reporter &reporter) {
  if (!var || !var->getDeclName().isIdentifier()) {
    return false;
  }
  const std::string name = var->getName().str();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

bool atomicType(QualType type) {
  if (type.isNull()) {
    return false;
  }
  if (type->isAtomicType() || type.getCanonicalType()->isAtomicType()) {
    return true;
  }
  const clang::TagDecl *tag = type->getAsTagDecl();
  if (!tag || !tag->getDeclName().isIdentifier() ||
      !tag->getName().contains_insensitive("atomic")) {
    return false;
  }
  const auto *space = llvm::dyn_cast<clang::NamespaceDecl>(tag->getDeclContext());
  return space && space->isStdNamespace();
}

bool sharedObject(const VarDecl *var) {
  if (!var || var->isImplicit() || !var->hasGlobalStorage()) {
    return false;
  }
  const QualType type = var->getType();
  if (type.isNull() || type.isConstQualified() || type.isVolatileQualified() || atomicType(type)) {
    return false;
  }
  return true;
}

Role roleOf(const FunctionDecl *fn) {
  if (!fn) {
    return Role::Plain;
  }
  if (isIsr(fn)) {
    return Role::Isr;
  }
  if (fn->isMain()) {
    return Role::Main;
  }
  if (!fn->getDeclName().isIdentifier()) {
    return Role::Plain;
  }
  if (fn->getName() == "main") {
    return Role::Main;
  }
  const llvm::StringRef name = fn->getName();
  if (name.ends_with("_task") || name.ends_with("_thread")) {
    return Role::Thread;
  }
  return Role::Plain;
}

const VarDecl *objectOf(const Expr *expr) {
  while (expr) {
    expr = expr->IgnoreParenImpCasts();
    if (const auto *ref = llvm::dyn_cast<DeclRefExpr>(expr)) {
      return llvm::dyn_cast<VarDecl>(ref->getDecl());
    }
    if (const auto *member = llvm::dyn_cast<clang::MemberExpr>(expr)) {
      expr = member->getBase();
      continue;
    }
    if (const auto *sub = llvm::dyn_cast<clang::ArraySubscriptExpr>(expr)) {
      expr = sub->getBase();
      continue;
    }
    break;
  }
  return nullptr;
}

struct Access {
  const FunctionDecl *fn = nullptr;
  bool write = false;
};

// A helper is the same context as whoever calls it. An ISR, main, and each
// *_task / *_thread are separate contexts. volatile, atomic, and a comment
// that names the protocol already tell that story.
class Collect : public clang::RecursiveASTVisitor<Collect> {
public:
  std::map<const VarDecl *, std::vector<Access>> uses;

  bool VisitFunctionDecl(FunctionDecl *fn) {
    if (!fn || !fn->isThisDeclarationADefinition() || !fn->getBody() || fn->isImplicit() ||
        fn->isTemplateInstantiation()) {
      return true;
    }
    Body body(fn, uses);
    body.TraverseStmt(fn->getBody());
    return true;
  }

private:
  class Body : public clang::RecursiveASTVisitor<Body> {
  public:
    Body(const FunctionDecl *fn, std::map<const VarDecl *, std::vector<Access>> &uses)
        : fn(fn), uses(uses) {}

    bool VisitDeclRefExpr(DeclRefExpr *ref) {
      note(ref ? llvm::dyn_cast<VarDecl>(ref->getDecl()) : nullptr, false);
      return true;
    }

    bool VisitBinaryOperator(BinaryOperator *op) {
      if (op && op->isAssignmentOp()) {
        note(objectOf(op->getLHS()), true);
      }
      return true;
    }

    bool VisitUnaryOperator(UnaryOperator *op) {
      if (op && op->isIncrementDecrementOp()) {
        note(objectOf(op->getSubExpr()), true);
      }
      return true;
    }

  private:
    const FunctionDecl *fn;
    std::map<const VarDecl *, std::vector<Access>> &uses;

    void note(const VarDecl *var, bool write) {
      if (!sharedObject(var)) {
        return;
      }
      uses[var->getCanonicalDecl()].push_back({fn, write});
    }
  };
};

bool commentBody(llvm::StringRef text, std::string &body) {
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '"' || c == '\'') {
      const char quote = c;
      ++i;
      while (i < text.size() && text[i] != quote && text[i] != '\n') {
        if (text[i] == '\\' && i + 1 < text.size()) {
          i += 2;
          continue;
        }
        ++i;
      }
      continue;
    }
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
      body = text.substr(i + 2).trim().str();
      return !body.empty();
    }
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
      const size_t end = text.find("*/", i + 2);
      body = text.slice(i + 2, end == llvm::StringRef::npos ? text.size() : end).trim().str();
      return !body.empty();
    }
  }
  return false;
}

llvm::StringRef lineAt(const SourceManager &sm, SourceLocation loc, bool previous) {
  loc = sm.getExpansionLoc(loc);
  if (loc.isInvalid()) {
    return {};
  }
  bool invalid = false;
  const llvm::StringRef buf = sm.getBufferData(sm.getFileID(loc), &invalid);
  if (invalid || sm.getFileOffset(loc) > buf.size()) {
    return {};
  }
  unsigned start = sm.getFileOffset(loc);
  while (start > 0 && buf[start - 1] != '\n') {
    --start;
  }
  if (previous) {
    if (start == 0) {
      return {};
    }
    unsigned prevEnd = start - 1;
    unsigned prevStart = prevEnd;
    while (prevStart > 0 && buf[prevStart - 1] != '\n') {
      --prevStart;
    }
    return buf.slice(prevStart, prevEnd);
  }
  unsigned end = start;
  while (end < buf.size() && buf[end] != '\n') {
    ++end;
  }
  return buf.slice(start, end);
}

bool namesProtocol(llvm::StringRef text) {
  std::string body;
  if (!commentBody(text, body)) {
    return false;
  }
  const llvm::StringRef comment(body);
  if (comment.contains_insensitive("critical") || comment.contains_insensitive("atomic") ||
      comment.contains_insensitive("queue") || comment.contains_insensitive("mutex") ||
      comment.contains_insensitive("sync")) {
    return true;
  }
  const size_t at = comment.find_insensitive("lock");
  return at != llvm::StringRef::npos && (at == 0 || comment[at - 1] != 'c');
}

bool documented(const VarDecl *var, const SourceManager &sm) {
  for (const VarDecl *redecl : var->redecls()) {
    const SourceLocation loc = redecl->getLocation();
    if (namesProtocol(lineAt(sm, loc, false)) || namesProtocol(lineAt(sm, loc, true))) {
      return true;
    }
  }
  return false;
}

bool crossesContext(const std::vector<Access> &accesses) {
  bool wrote = false;
  std::set<const FunctionDecl *> isrs;
  std::set<const FunctionDecl *> threads;
  std::set<const FunctionDecl *> accessors;
  bool main = false;
  for (const Access &access : accesses) {
    if (!access.fn) {
      continue;
    }
    wrote = wrote || access.write;
    const FunctionDecl *fn = access.fn->getCanonicalDecl();
    accessors.insert(fn);
    switch (roleOf(fn)) {
    case Role::Isr:
      isrs.insert(fn);
      break;
    case Role::Thread:
      threads.insert(fn);
      break;
    case Role::Main:
      main = true;
      break;
    case Role::Plain:
      break;
    }
  }
  if (!wrote) {
    return false;
  }
  if (!isrs.empty() && (isrs.size() > 1 || accessors.size() > isrs.size())) {
    return true;
  }
  if (threads.size() > 1) {
    return true;
  }
  return !threads.empty() && main;
}

} // namespace

void NoDataRaceCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void NoDataRaceCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *tu = result.Nodes.getNodeAs<TranslationUnitDecl>("tu");
  if (!tu || !result.SourceManager) {
    return;
  }
  Collect collect;
  collect.TraverseDecl(const_cast<TranslationUnitDecl *>(tu));
  for (const auto &item : collect.uses) {
    const VarDecl *var = item.first;
    if (!crossesContext(item.second) || documented(var, *result.SourceManager) ||
        allowed(var, reporter)) {
      continue;
    }
    const VarDecl *def = var->getDefinition();
    if (!def) {
      def = var;
    }
    reporter.emit(*result.SourceManager, def->getLocation(), kId, kMsg);
  }
}
