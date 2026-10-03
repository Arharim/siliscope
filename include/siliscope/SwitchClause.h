#pragma once

// Shared shape of a switch for ss.ctrl.switch-well-formed and
// ss.ctrl.switch-fallthrough-comment. A clause is one run of top-level labels
// (`case 1: case 2:`) plus the statements after them, up to the next label
// that is a direct child of the switch body.

#include "clang/AST/Attr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace siliscope {
namespace switch_clause {

struct Clause {
  const clang::SwitchCase *label = nullptr;
  llvm::SmallVector<const clang::Stmt *, 4> stmts;
  const clang::SwitchCase *next = nullptr;
};

inline bool isFallThrough(const clang::AttributedStmt *stmt) {
  if (!stmt) {
    return false;
  }
  for (const clang::Attr *attr : stmt->getAttrs()) {
    if (llvm::isa<clang::FallThroughAttr>(attr)) {
      return true;
    }
  }
  return false;
}

inline bool isVacuous(const clang::Stmt *stmt) {
  if (!stmt || llvm::isa<clang::NullStmt>(stmt)) {
    return true;
  }
  if (const auto *comp = llvm::dyn_cast<clang::CompoundStmt>(stmt)) {
    for (const clang::Stmt *child : comp->body()) {
      if (!isVacuous(child)) {
        return false;
      }
    }
    return true;
  }
  if (const auto *attr = llvm::dyn_cast<clang::AttributedStmt>(stmt)) {
    // The attribute is the annotation, not an empty statement.
    if (isFallThrough(attr)) {
      return false;
    }
    return isVacuous(attr->getSubStmt());
  }
  return false;
}

inline bool clauseEmpty(const Clause &clause) {
  for (const clang::Stmt *stmt : clause.stmts) {
    if (!isVacuous(stmt)) {
      return false;
    }
  }
  return true;
}

// Last statement only. Braces wrap that statement (`case 1: { work(); break; }`).
// An if whose arms both break is dataflow, so it does not count.
inline const clang::Stmt *effectiveLast(const clang::Stmt *stmt) {
  while (stmt) {
    if (const auto *attr = llvm::dyn_cast<clang::AttributedStmt>(stmt)) {
      if (isFallThrough(attr)) {
        return attr;
      }
      stmt = attr->getSubStmt();
      continue;
    }
    if (const auto *comp = llvm::dyn_cast<clang::CompoundStmt>(stmt)) {
      if (comp->body_empty()) {
        return comp;
      }
      stmt = comp->body_back();
      continue;
    }
    return stmt;
  }
  return nullptr;
}

// break, return, and throw leave the switch. continue and goto do not.
// A fall-through attribute is the annotation the other rule asks for.
inline bool clauseTerminates(const Clause &clause) {
  if (clause.stmts.empty()) {
    return false;
  }
  const clang::Stmt *last = effectiveLast(clause.stmts.back());
  if (!last) {
    return false;
  }
  if (llvm::isa<clang::BreakStmt>(last) || llvm::isa<clang::ReturnStmt>(last) ||
      llvm::isa<clang::CXXThrowExpr>(last)) {
    return true;
  }
  return isFallThrough(llvm::dyn_cast<clang::AttributedStmt>(last));
}

inline llvm::SmallPtrSet<const clang::SwitchCase *, 16> topLevelCases(
    const clang::CompoundStmt *body) {
  llvm::SmallPtrSet<const clang::SwitchCase *, 16> allowed;
  for (const clang::Stmt *child : body->body()) {
    const clang::Stmt *stmt = child;
    while (const auto *label = llvm::dyn_cast_or_null<clang::SwitchCase>(stmt)) {
      allowed.insert(label);
      stmt = label->getSubStmt();
    }
  }
  return allowed;
}

inline void collectClauses(const clang::CompoundStmt *body, llvm::SmallVectorImpl<Clause> &out) {
  const auto children = body->body();
  for (auto it = children.begin(); it != children.end(); ++it) {
    const auto *label = llvm::dyn_cast<clang::SwitchCase>(*it);
    if (!label) {
      continue;
    }
    Clause clause;
    clause.label = label;
    const clang::Stmt *inner = label->getSubStmt();
    while (const auto *nested = llvm::dyn_cast_or_null<clang::SwitchCase>(inner)) {
      inner = nested->getSubStmt();
    }
    if (inner) {
      clause.stmts.push_back(inner);
    }
    for (auto next = it + 1; next != children.end(); ++next) {
      if (const auto *following = llvm::dyn_cast<clang::SwitchCase>(*next)) {
        clause.next = following;
        break;
      }
      clause.stmts.push_back(*next);
    }
    out.push_back(clause);
  }
}

inline bool hasFallthroughComment(const Clause &clause,
                                  const clang::SourceManager &sm,
                                  const clang::LangOptions &lang) {
  if (clause.stmts.empty() || !clause.next) {
    return false;
  }
  const clang::Stmt *leaf = effectiveLast(clause.stmts.back());
  if (!leaf) {
    return false;
  }
  clang::SourceLocation begin = leaf->getEndLoc();
  if (begin.isMacroID()) {
    begin = sm.getExpansionLoc(begin);
  }
  begin = clang::Lexer::getLocForEndOfToken(begin, 0, sm, lang);
  clang::SourceLocation end = clause.next->getBeginLoc();
  if (end.isMacroID()) {
    end = sm.getExpansionLoc(end);
  }
  if (begin.isInvalid() || end.isInvalid() || !sm.isBeforeInTranslationUnit(begin, end)) {
    return false;
  }
  const llvm::StringRef text =
      clang::Lexer::getSourceText(clang::CharSourceRange::getCharRange(begin, end), sm, lang);
  return text.contains_insensitive("fallthrough") || text.contains_insensitive("fall through") ||
         text.contains_insensitive("fall-through") || text.contains_insensitive("falls through") ||
         text.contains_insensitive("fallthru");
}

} // namespace switch_clause
} // namespace siliscope
