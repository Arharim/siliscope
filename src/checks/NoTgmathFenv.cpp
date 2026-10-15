#include "siliscope/NoTgmathFenv.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <cctype>
#include <string>

using clang::CallExpr;
using clang::DynTypedNode;
using clang::FileID;
using clang::FunctionDecl;
using clang::SourceLocation;
using clang::SourceManager;
using clang::ast_matchers::callee;
using clang::ast_matchers::callExpr;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::hasAnyName;
using clang::ast_matchers::translationUnitDecl;

namespace {

constexpr const char *kId = "ss.libc.no-tgmath-fenv";
constexpr const char *kMsg = "do not use tgmath.h or fenv.h";

bool projectFile(const SourceManager &sm, FileID file) {
  if (file.isInvalid()) {
    return false;
  }
  const SourceLocation start = sm.getLocForStartOfFile(file);
  return start.isValid() && !sm.isInSystemHeader(start) && !sm.isWrittenInBuiltinFile(start) &&
         !sm.isWrittenInScratchSpace(start) && !sm.isInPredefinedFile(start) &&
         !sm.isWrittenInCommandLineFile(start);
}

bool projectDefinition(const FunctionDecl *fn) {
  for (const FunctionDecl *redecl : fn->redecls()) {
    if (redecl->isThisDeclarationADefinition() && !redecl->isImplicit()) {
      return true;
    }
  }
  return false;
}

bool allowedName(const char *name, Reporter &reporter) {
  return name && name[0] != '\0' && reporter.allows(kId, name);
}

bool allowedCall(const clang::Expr *expr, clang::ASTContext &ctx, Reporter &reporter) {
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
      return allowedName(name.c_str(), reporter);
    }
  }
  return false;
}

bool includeLine(llvm::StringRef line, llvm::StringRef header) {
  llvm::StringRef rest = line.ltrim();
  if (!rest.starts_with("#")) {
    return false;
  }
  rest = rest.drop_front(1).ltrim();
  if (!rest.starts_with("include") ||
      (rest.size() > 7 && std::isalnum(static_cast<unsigned char>(rest[7])))) {
    return false;
  }
  rest = rest.drop_front(7).ltrim();
  if (rest.empty() || (rest.front() != '<' && rest.front() != '"')) {
    return false;
  }
  const char close = rest.front() == '<' ? '>' : '"';
  rest = rest.drop_front(1);
  const size_t end = rest.find(close);
  return end != llvm::StringRef::npos && rest.take_front(end) == header;
}

void scanHeader(const SourceManager &sm, Reporter &reporter, llvm::StringRef header) {
  if (allowedName(std::string(header).c_str(), reporter)) {
    return;
  }
  for (auto it = sm.fileinfo_begin(); it != sm.fileinfo_end(); ++it) {
    const FileID file = sm.translateFile(it->getFirst());
    if (!projectFile(sm, file)) {
      continue;
    }
    bool invalid = false;
    const llvm::StringRef buf = sm.getBufferData(file, &invalid);
    if (invalid || buf.empty()) {
      continue;
    }
    bool block = false;
    unsigned lineStart = 0;
    for (unsigned i = 0; i <= buf.size(); ++i) {
      const bool end = i == buf.size() || buf[i] == '\n';
      if (!end) {
        continue;
      }
      if (!block && includeLine(buf.slice(lineStart, i), header)) {
        reporter.emit(sm, sm.getComposedLoc(file, lineStart), kId, kMsg);
      }
      for (unsigned j = lineStart; j < i && j < buf.size(); ++j) {
        if (!block && buf[j] == '/' && j + 1 < buf.size() && buf[j + 1] == '*') {
          block = true;
          ++j;
        } else if (block && buf[j] == '*' && j + 1 < buf.size() && buf[j + 1] == '/') {
          block = false;
          ++j;
        }
      }
      lineStart = i + 1;
    }
  }
}

} // namespace

void NoTgmathFenvCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(translationUnitDecl().bind("tu"), this);
  finder.addMatcher(callExpr(callee(functionDecl(hasAnyName("feclearexcept",
                                                            "fegetexceptflag",
                                                            "feraiseexcept",
                                                            "fesetexceptflag",
                                                            "fetestexcept",
                                                            "fegetround",
                                                            "fesetround",
                                                            "fegetenv",
                                                            "feholdexcept",
                                                            "fesetenv",
                                                            "feupdateenv",
                                                            "feenableexcept",
                                                            "fedisableexcept",
                                                            "fegetexcept"))))
                        .bind("call"),
                    this);
}

void NoTgmathFenvCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.SourceManager || !result.Context || result.Context->getLangOpts().CPlusPlus) {
    return;
  }
  if (result.Nodes.getNodeAs<clang::TranslationUnitDecl>("tu")) {
    scanHeader(*result.SourceManager, reporter, "tgmath.h");
    scanHeader(*result.SourceManager, reporter, "fenv.h");
    return;
  }
  const auto *call = result.Nodes.getNodeAs<CallExpr>("call");
  const FunctionDecl *fn = call ? call->getDirectCallee() : nullptr;
  if (!fn || llvm::isa<clang::CXXMethodDecl>(fn) || projectDefinition(fn)) {
    return;
  }
  if (fn->getFormalLinkage() == clang::Linkage::Internal ||
      allowedCall(call, *result.Context, reporter)) {
    return;
  }
  reporter.emit(*result.SourceManager, call->getBeginLoc(), kId, kMsg);
}
