#include "siliscope/SeqCst.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include <string>

using clang::DeclRefExpr;
using clang::DynTypedNode;
using clang::EnumConstantDecl;
using clang::FunctionDecl;
using clang::SourceLocation;
using clang::SourceManager;
using clang::ast_matchers::declRefExpr;
using clang::ast_matchers::integerLiteral;

namespace {

constexpr const char *kId = "ss.conc.seq-cst-or-documented";
constexpr const char *kMsg = "document this non-sequential atomic order";

bool fullName(llvm::StringRef name) {
  return name == "memory_order_relaxed" || name == "memory_order_consume" ||
         name == "memory_order_acquire" || name == "memory_order_release" ||
         name == "memory_order_acq_rel" || name == "__ATOMIC_RELAXED" ||
         name == "__ATOMIC_CONSUME" || name == "__ATOMIC_ACQUIRE" || name == "__ATOMIC_RELEASE" ||
         name == "__ATOMIC_ACQ_REL";
}

bool shortName(llvm::StringRef name) {
  return name == "relaxed" || name == "consume" || name == "acquire" || name == "release" ||
         name == "acq_rel";
}

bool identifier(const clang::NamedDecl *decl, llvm::StringRef &name) {
  if (!decl || !decl->getDeclName().isIdentifier()) {
    return false;
  }
  name = decl->getName();
  return true;
}

bool interesting(const clang::NamedDecl *decl) {
  if (!decl) {
    return false;
  }
  llvm::StringRef name;
  if (const auto *enumer = llvm::dyn_cast<EnumConstantDecl>(decl)) {
    if (!identifier(enumer, name)) {
      return false;
    }
    if (fullName(name)) {
      return true;
    }
    const auto *parent = llvm::dyn_cast<clang::EnumDecl>(enumer->getDeclContext());
    llvm::StringRef parentName;
    return shortName(name) && identifier(parent, parentName) &&
           parentName.contains_insensitive("memory_order");
  }
  return identifier(decl, name) && fullName(name);
}

bool commentText(llvm::StringRef text) {
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
      const llvm::StringRef body = text.substr(i + 2).trim();
      return !body.empty();
    }
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
      const size_t end = text.find("*/", i + 2);
      const llvm::StringRef body =
          text.slice(i + 2, end == llvm::StringRef::npos ? text.size() : end);
      return !body.trim().empty();
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
  if (invalid) {
    return {};
  }
  unsigned offset = sm.getFileOffset(loc);
  if (offset > buf.size()) {
    return {};
  }
  unsigned start = offset;
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
  unsigned end = offset;
  while (end < buf.size() && buf[end] != '\n') {
    ++end;
  }
  return buf.slice(start, end);
}

bool documented(SourceLocation loc, const SourceManager &sm) {
  if (loc.isInvalid()) {
    return false;
  }
  return commentText(lineAt(sm, loc, false)) || commentText(lineAt(sm, loc, true));
}

bool allowed(const clang::Expr *expr, clang::ASTContext &ctx, Reporter &reporter) {
  DynTypedNode node = DynTypedNode::create(*expr);
  for (int step = 0; step < 32; ++step) {
    const auto parents = ctx.getParents(node);
    if (parents.empty()) {
      return false;
    }
    node = parents[0];
    if (const auto *fn = node.get<FunctionDecl>()) {
      if (fn->getDeclName().isEmpty()) {
        return false;
      }
      const std::string name = fn->getDeclName().getAsString();
      return !name.empty() && reporter.allows(kId, name.c_str());
    }
  }
  return false;
}

} // namespace

void SeqCstCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(declRefExpr().bind("ref"), this);
  finder.addMatcher(integerLiteral().bind("lit"), this);
}

void SeqCstCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.SourceManager || !result.Context) {
    return;
  }
  const SourceManager &sm = *result.SourceManager;
  if (const auto *ref = result.Nodes.getNodeAs<DeclRefExpr>("ref")) {
    if (!interesting(ref->getDecl()) || documented(ref->getLocation(), sm) ||
        allowed(ref, *result.Context, reporter)) {
      return;
    }
    reporter.emit(sm, ref->getLocation(), kId, kMsg);
    return;
  }
  const auto *lit = result.Nodes.getNodeAs<clang::IntegerLiteral>("lit");
  if (!lit || !lit->getBeginLoc().isMacroID()) {
    return;
  }
  const llvm::StringRef macro =
      clang::Lexer::getImmediateMacroName(lit->getBeginLoc(), sm, result.Context->getLangOpts());
  if (!fullName(macro) || documented(lit->getBeginLoc(), sm) ||
      allowed(lit, *result.Context, reporter)) {
    return;
  }
  reporter.emit(sm, lit->getBeginLoc(), kId, kMsg);
}
