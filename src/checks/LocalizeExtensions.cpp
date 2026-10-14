#include "siliscope/LocalizeExtensions.h"

#include "siliscope/Report.h"

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <string>
#include <vector>

using clang::Decl;
using clang::FileID;
using clang::FunctionDecl;
using clang::NamedDecl;
using clang::ParmVarDecl;
using clang::SourceLocation;
using clang::SourceManager;
using clang::SourceRange;
using clang::ast_matchers::translationUnitDecl;

namespace {

constexpr const char *kId = "ss.emb.localize-extensions";
constexpr const char *kMsg = "keep this extension in the hardware layer";

bool projectMain(const SourceManager &sm, FileID file) {
  if (file.isInvalid() || file != sm.getMainFileID()) {
    return false;
  }
  const SourceLocation start = sm.getLocForStartOfFile(file);
  return start.isValid() && !sm.isInSystemHeader(start) && !sm.isWrittenInBuiltinFile(start) &&
         !sm.isWrittenInScratchSpace(start) && !sm.isWrittenInCommandLineFile(start);
}

bool contains(const SourceManager &sm, SourceRange range, SourceLocation loc) {
  if (range.isInvalid() || loc.isInvalid()) {
    return false;
  }
  const SourceLocation begin = sm.getSpellingLoc(range.getBegin());
  const SourceLocation end = sm.getSpellingLoc(range.getEnd());
  const SourceLocation point = sm.getSpellingLoc(loc);
  if (begin.isInvalid() || end.isInvalid() || point.isInvalid() ||
      sm.getFileID(begin) != sm.getFileID(point)) {
    return false;
  }
  const unsigned at = sm.getFileOffset(point);
  return sm.getFileOffset(begin) <= at && at <= sm.getFileOffset(end);
}

const NamedDecl *lift(const NamedDecl *decl) {
  if (const auto *param = llvm::dyn_cast_or_null<ParmVarDecl>(decl)) {
    if (const auto *fn = llvm::dyn_cast<FunctionDecl>(param->getDeclContext())) {
      return fn;
    }
  }
  return decl;
}

// A GNU attribute often sits just outside the decl's own source range.
const NamedDecl *ownerOf(const SourceManager &sm,
                         const std::vector<const NamedDecl *> &decls,
                         SourceLocation loc) {
  const NamedDecl *best = nullptr;
  unsigned bestSpan = ~0U;
  for (const NamedDecl *decl : decls) {
    if (!contains(sm, decl->getSourceRange(), loc)) {
      continue;
    }
    const SourceLocation begin = sm.getSpellingLoc(decl->getSourceRange().getBegin());
    const SourceLocation end = sm.getSpellingLoc(decl->getSourceRange().getEnd());
    const unsigned span = sm.getFileOffset(end) - sm.getFileOffset(begin);
    if (span < bestSpan) {
      best = decl;
      bestSpan = span;
    }
  }
  if (best) {
    return lift(best);
  }
  for (const NamedDecl *decl : decls) {
    if (!decl->hasAttrs()) {
      continue;
    }
    for (const clang::Attr *attr : decl->attrs()) {
      if (!attr || attr->isImplicit() || !contains(sm, attr->getRange(), loc)) {
        continue;
      }
      return lift(decl);
    }
  }
  // The spelling of a trailing attribute can start after __attribute__.
  const SourceLocation point = sm.getSpellingLoc(loc);
  if (point.isInvalid()) {
    return nullptr;
  }
  const unsigned at = sm.getFileOffset(point);
  const NamedDecl *near = nullptr;
  unsigned gap = 48;
  for (const NamedDecl *decl : decls) {
    if (!decl->hasAttrs()) {
      continue;
    }
    for (const clang::Attr *attr : decl->attrs()) {
      if (!attr || attr->isImplicit()) {
        continue;
      }
      const SourceLocation begin = sm.getSpellingLoc(attr->getLocation());
      if (begin.isInvalid() || sm.getFileID(begin) != sm.getFileID(point)) {
        continue;
      }
      const unsigned pos = sm.getFileOffset(begin);
      if (pos < at || pos - at >= gap) {
        continue;
      }
      near = decl;
      gap = pos - at;
    }
  }
  return lift(near);
}

bool allowed(const NamedDecl *decl, Reporter &reporter) {
  if (!decl || decl->getDeclName().isEmpty()) {
    return false;
  }
  const std::string name = decl->getDeclName().getAsString();
  return !name.empty() && reporter.allows(kId, name.c_str());
}

bool identAt(llvm::StringRef buf, size_t i, llvm::StringRef word) {
  if (!buf.substr(i).starts_with(word)) {
    return false;
  }
  const auto ident = [](char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
  };
  if (i > 0 && ident(buf[i - 1])) {
    return false;
  }
  const size_t end = i + word.size();
  return end >= buf.size() || !ident(buf[end]);
}

class CollectNames : public clang::RecursiveASTVisitor<CollectNames> {
public:
  std::vector<const NamedDecl *> decls;

  bool VisitNamedDecl(NamedDecl *decl) {
    if (decl && !decl->isImplicit() && !decl->getDeclName().isEmpty()) {
      decls.push_back(decl);
    }
    return true;
  }
};

} // namespace

void LocalizeExtensionsCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void LocalizeExtensionsCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *tu = result.Nodes.getNodeAs<clang::TranslationUnitDecl>("tu");
  if (!tu || !result.SourceManager || !result.Context) {
    return;
  }
  const SourceManager &sm = *result.SourceManager;
  const FileID main = sm.getMainFileID();
  if (!projectMain(sm, main)) {
    return;
  }
  bool invalid = false;
  const llvm::StringRef buf = sm.getBufferData(main, &invalid);
  if (invalid || buf.empty()) {
    return;
  }
  CollectNames names;
  names.TraverseDecl(const_cast<clang::TranslationUnitDecl *>(tu));

  const auto emitAt = [&](unsigned offset) {
    const SourceLocation loc =
        sm.getLocForStartOfFile(main).getLocWithOffset(static_cast<int>(offset));
    if (allowed(ownerOf(sm, names.decls, loc), reporter)) {
      return;
    }
    reporter.emit(sm, loc, kId, kMsg);
  };

  bool lineStart = true;
  bool inLine = false;
  bool inBlock = false;
  char quote = 0;
  for (size_t i = 0; i < buf.size(); ++i) {
    const char c = buf[i];
    if (quote) {
      if (c == '\\' && i + 1 < buf.size()) {
        ++i;
        continue;
      }
      if (c == quote) {
        quote = 0;
      }
      lineStart = c == '\n';
      continue;
    }
    if (inLine) {
      if (c == '\n') {
        inLine = false;
        lineStart = true;
      }
      continue;
    }
    if (inBlock) {
      if (c == '*' && i + 1 < buf.size() && buf[i + 1] == '/') {
        inBlock = false;
        ++i;
      }
      lineStart = c == '\n';
      continue;
    }
    if (c == '/' && i + 1 < buf.size() && buf[i + 1] == '/') {
      inLine = true;
      ++i;
      continue;
    }
    if (c == '/' && i + 1 < buf.size() && buf[i + 1] == '*') {
      inBlock = true;
      ++i;
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
      lineStart = false;
      continue;
    }
    if (c == '\n') {
      lineStart = true;
      continue;
    }
    if (lineStart && (c == ' ' || c == '\t' || c == '\r')) {
      continue;
    }
    if (lineStart && c == '#') {
      size_t j = i + 1;
      while (j < buf.size() && (buf[j] == ' ' || buf[j] == '\t')) {
        ++j;
      }
      if (identAt(buf, j, "pragma")) {
        emitAt(static_cast<unsigned>(i));
      }
    }
    lineStart = false;
    if (identAt(buf, i, "__attribute__") || identAt(buf, i, "__declspec") ||
        identAt(buf, i, "__packed") || identAt(buf, i, "__weak") || identAt(buf, i, "__irq") ||
        identAt(buf, i, "__asm__") || identAt(buf, i, "__asm") || identAt(buf, i, "asm")) {
      emitAt(static_cast<unsigned>(i));
      continue;
    }
  }
}
