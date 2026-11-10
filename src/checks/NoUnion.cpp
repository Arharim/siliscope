#include "siliscope/NoUnion.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/StringExtras.h"

#include <string>

using clang::RecordDecl;
using clang::SourceLocation;
using clang::SourceManager;
using clang::ast_matchers::isDefinition;
using clang::ast_matchers::isUnion;
using clang::ast_matchers::recordDecl;

namespace {

constexpr const char *kId = "ss.type.no-union";
constexpr const char *kMsg = "do not use a union unless it is a documented wire or register map";

bool project(const SourceManager &sm, SourceLocation loc) {
  if (loc.isInvalid()) {
    return false;
  }
  loc = sm.getSpellingLoc(loc);
  return loc.isValid() && !sm.isInSystemHeader(loc) && !sm.isWrittenInBuiltinFile(loc) &&
         !sm.isWrittenInScratchSpace(loc) && !sm.isInPredefinedFile(loc) &&
         !sm.isWrittenInCommandLineFile(loc) && sm.getFileEntryForID(sm.getFileID(loc)) != nullptr;
}

bool hasWord(llvm::StringRef text, std::initializer_list<llvm::StringRef> words) {
  std::string lower = text.lower();
  auto token = [&](size_t i) {
    const char c = lower[i];
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
  };
  for (size_t i = 0; i < lower.size();) {
    if (!token(i)) {
      ++i;
      continue;
    }
    const size_t begin = i;
    while (i < lower.size() && token(i)) {
      ++i;
    }
    const llvm::StringRef word(lower.data() + begin, i - begin);
    for (llvm::StringRef want : words) {
      if (word == want) {
        return true;
      }
    }
  }
  return false;
}

std::string around(const SourceManager &sm, SourceLocation loc) {
  loc = sm.getSpellingLoc(loc);
  if (loc.isInvalid()) {
    return {};
  }
  bool invalid = false;
  const llvm::StringRef buf = sm.getBufferData(sm.getFileID(loc), &invalid);
  if (invalid) {
    return {};
  }
  const unsigned offset = sm.getFileOffset(loc);
  const unsigned back = offset > 400 ? offset - 400 : 0;
  llvm::StringRef before = buf.slice(back, offset);
  const size_t brace = before.rfind("\n}");
  if (brace != llvm::StringRef::npos) {
    before = before.substr(brace + 1);
  }
  const unsigned end = offset + 240 < buf.size() ? offset + 240 : static_cast<unsigned>(buf.size());
  std::string text(before.data(), before.size());
  text.append(buf.data() + offset, buf.data() + end);
  return text;
}

bool documentedOverlay(const SourceManager &sm, const RecordDecl *decl) {
  return hasWord(around(sm, decl->getBeginLoc()),
                 {"hardware", "register", "wire", "protocol", "packet"});
}

} // namespace

void NoUnionCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(recordDecl(isUnion(), isDefinition()).bind("union"), this);
}

void NoUnionCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  const auto *decl = result.Nodes.getNodeAs<RecordDecl>("union");
  if (!decl || !result.SourceManager || decl->isImplicit()) {
    return;
  }
  const SourceManager &sm = *result.SourceManager;
  if (!project(sm, decl->getBeginLoc()) || documentedOverlay(sm, decl)) {
    return;
  }
  const std::string name = decl->getIdentifier() ? decl->getName().str() : std::string();
  if (!name.empty() && reporter.allows(kId, name.c_str())) {
    return;
  }
  const SourceLocation loc =
      decl->getLocation().isValid() ? decl->getLocation() : decl->getBeginLoc();
  reporter.emit(sm, loc, kId, kMsg);
}
