#include "siliscope/OnePerLine.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

#include <string>
#include <utility>

using clang::DeclaratorDecl;
using clang::ParmVarDecl;
using clang::SourceLocation;
using clang::SourceManager;
using clang::ast_matchers::translationUnitDecl;

namespace {

constexpr const char *kId = "ss.decl.one-per-line";
constexpr const char *kMsg = "declare one identifier per statement";

bool allowed(const DeclaratorDecl *decl, Reporter &reporter) {
  if (!decl || decl->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = decl->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

struct Key {
  unsigned file = 0;
  unsigned offset = 0;
  bool operator==(const Key &other) const { return file == other.file && offset == other.offset; }
};

struct KeyInfo {
  static Key getEmptyKey() { return {~0U, ~0U}; }
  static Key getTombstoneKey() { return {~0U - 1, ~0U - 1}; }
  static unsigned getHashValue(const Key &key) { return (key.file * 16777619U) ^ key.offset; }
  static bool isEqual(const Key &lhs, const Key &rhs) { return lhs == rhs; }
};

class Visit : public clang::RecursiveASTVisitor<Visit> {
public:
  explicit Visit(const SourceManager &sm) : sm(sm) {}

  bool VisitDeclaratorDecl(DeclaratorDecl *decl) {
    if (!decl || decl->isImplicit() || llvm::isa<ParmVarDecl>(decl) ||
        decl->getDeclName().isEmpty()) {
      return true;
    }
    // Spelling would collapse every use of a type macro onto one token.
    const SourceLocation spec = sm.getFileLoc(decl->getTypeSpecStartLoc());
    if (spec.isInvalid()) {
      return true;
    }
    const Key key{sm.getFileID(spec).getHashValue(), sm.getFileOffset(spec)};
    groups[key].push_back(decl);
    return true;
  }

  const SourceManager &sm;
  llvm::DenseMap<Key, llvm::SmallVector<const DeclaratorDecl *, 4>, KeyInfo> groups;
};

} // namespace

void OnePerLineCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void OnePerLineCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *tu = result.Nodes.getNodeAs<clang::TranslationUnitDecl>("tu");
  if (!tu || !result.SourceManager) {
    return;
  }
  Visit visit(*result.SourceManager);
  visit.TraverseDecl(const_cast<clang::TranslationUnitDecl *>(tu));
  for (const auto &group : visit.groups) {
    const auto &decls = group.second;
    if (decls.size() < 2) {
      continue;
    }
    for (unsigned i = 1; i < decls.size(); ++i) {
      if (allowed(decls[i], reporter)) {
        continue;
      }
      reporter.emit(*result.SourceManager, decls[i]->getLocation(), kId, kMsg);
    }
  }
}
