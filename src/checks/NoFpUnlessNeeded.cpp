#include "siliscope/NoFpUnlessNeeded.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Path.h"

#include <string>

using clang::FieldDecl;
using clang::FileID;
using clang::FloatingLiteral;
using clang::FunctionDecl;
using clang::QualType;
using clang::SourceLocation;
using clang::SourceManager;
using clang::TypedefNameDecl;
using clang::VarDecl;
using clang::ast_matchers::translationUnitDecl;

namespace {

constexpr const char *kId = "ss.type.no-fp-unless-needed";
constexpr const char *kMsg = "mark this module numeric, or keep floating point out";

bool project(const SourceManager &sm, SourceLocation loc) {
  if (loc.isInvalid()) {
    return false;
  }
  loc = sm.getSpellingLoc(loc);
  if (loc.isInvalid() || sm.isInSystemHeader(loc) || sm.isWrittenInBuiltinFile(loc) ||
      sm.isWrittenInScratchSpace(loc) || sm.isInPredefinedFile(loc) ||
      sm.isWrittenInCommandLineFile(loc)) {
    return false;
  }
  return sm.getFileEntryForID(sm.getFileID(loc)) != nullptr;
}

bool floating(QualType type) {
  if (type.isNull() || type->isDependentType()) {
    return false;
  }
  type = type.getNonReferenceType();
  if (const auto *array = type->getAsArrayTypeUnsafe()) {
    return floating(array->getElementType());
  }
  if (type->isPointerType() || type->isReferenceType()) {
    return floating(type->getPointeeType());
  }
  return type->isRealFloatingType();
}

llvm::StringRef trim(llvm::StringRef text) {
  return text.trim(" \t\r\n");
}

// The comment is exactly "numeric". "numeric keypad" and "non-numeric" stay closed.
bool numericComment(llvm::StringRef buf) {
  bool line = false;
  bool block = false;
  char quote = 0;
  std::string text;
  const auto finish = [&] {
    const bool hit = trim(text) == "numeric";
    text.clear();
    return hit;
  };
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
      continue;
    }
    if (line) {
      if (c == '\n') {
        if (finish()) {
          return true;
        }
        line = false;
      } else {
        text.push_back(c);
      }
      continue;
    }
    if (block) {
      if (c == '*' && i + 1 < buf.size() && buf[i + 1] == '/') {
        if (finish()) {
          return true;
        }
        block = false;
        ++i;
      } else {
        text.push_back(c);
      }
      continue;
    }
    if (c == '"' || c == '\'') {
      quote = c;
      continue;
    }
    if (c == '/' && i + 1 < buf.size() && buf[i + 1] == '/') {
      line = true;
      ++i;
      continue;
    }
    if (c == '/' && i + 1 < buf.size() && buf[i + 1] == '*') {
      block = true;
      ++i;
      continue;
    }
  }
  return line && finish();
}

bool fileMarked(const SourceManager &sm, FileID file) {
  if (!project(sm, sm.getLocForStartOfFile(file))) {
    return false;
  }
  bool invalid = false;
  const llvm::StringRef buf = sm.getBufferData(file, &invalid);
  return !invalid && numericComment(buf);
}

bool moduleMarked(const SourceManager &sm) {
  const FileID main = sm.getMainFileID();
  if (fileMarked(sm, main)) {
    return true;
  }
  const llvm::StringRef stem = llvm::sys::path::stem(sm.getFilename(sm.getLocForStartOfFile(main)));
  if (stem.empty()) {
    return false;
  }
  const std::string header = (stem + ".h").str();
  const std::string headerpp = (stem + ".hpp").str();
  for (auto it = sm.fileinfo_begin(); it != sm.fileinfo_end(); ++it) {
    const FileID file = sm.translateFile(it->getFirst());
    if (file == main || !fileMarked(sm, file)) {
      continue;
    }
    const llvm::StringRef name =
        llvm::sys::path::filename(sm.getFilename(sm.getLocForStartOfFile(file)));
    if (name == header || name == headerpp) {
      return true;
    }
  }
  return false;
}

class Find : public clang::RecursiveASTVisitor<Find> {
public:
  Find(const SourceManager &sm) : sm(sm) {}

  bool VisitFunctionDecl(FunctionDecl *fn) {
    if (fn && !fn->isImplicit() && floating(fn->getReturnType())) {
      note(fn->getBeginLoc());
    }
    return true;
  }

  bool VisitVarDecl(VarDecl *var) {
    if (var && !var->isImplicit() && floating(var->getType())) {
      note(var->getLocation());
    }
    return true;
  }

  bool VisitFieldDecl(FieldDecl *field) {
    if (field && !field->isImplicit() && floating(field->getType())) {
      note(field->getLocation());
    }
    return true;
  }

  bool VisitTypedefNameDecl(TypedefNameDecl *decl) {
    if (decl && floating(decl->getUnderlyingType())) {
      note(decl->getLocation());
    }
    return true;
  }

  bool VisitFloatingLiteral(FloatingLiteral *lit) {
    if (lit) {
      note(lit->getBeginLoc());
    }
    return true;
  }

  SourceLocation main;
  SourceLocation any;

private:
  void note(SourceLocation loc) {
    if (!project(sm, loc)) {
      return;
    }
    loc = sm.getSpellingLoc(loc);
    if (sm.getFileID(loc) == sm.getMainFileID()) {
      if (!main.isValid() || sm.isBeforeInTranslationUnit(loc, main)) {
        main = loc;
      }
    }
    if (!any.isValid() || sm.isBeforeInTranslationUnit(loc, any)) {
      any = loc;
    }
  }

  const SourceManager &sm;
};

} // namespace

void NoFpUnlessNeededCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(translationUnitDecl().bind("tu"), this);
}

void NoFpUnlessNeededCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.Context || !result.SourceManager ||
      !result.Nodes.getNodeAs<clang::TranslationUnitDecl>("tu")) {
    return;
  }
  const SourceManager &sm = *result.SourceManager;
  if (moduleMarked(sm)) {
    return;
  }
  Find found(sm);
  found.TraverseDecl(result.Context->getTranslationUnitDecl());
  const SourceLocation at = found.main.isValid() ? found.main : found.any;
  if (at.isValid()) {
    reporter.emit(sm, at, kId, kMsg);
  }
}
