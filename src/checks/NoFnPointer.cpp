#include "siliscope/NoFnPointer.h"

#include "siliscope/Report.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceManager.h"

#include <string>

using clang::FieldDecl;
using clang::FunctionDecl;
using clang::ParmVarDecl;
using clang::QualType;
using clang::SourceLocation;
using clang::SourceManager;
using clang::TypedefNameDecl;
using clang::ValueDecl;
using clang::VarDecl;
using clang::ast_matchers::fieldDecl;
using clang::ast_matchers::functionDecl;
using clang::ast_matchers::typedefNameDecl;
using clang::ast_matchers::varDecl;

namespace {

constexpr const char *kId = "ss.ptr.no-fn-pointer";
constexpr const char *kMsg = "do not hide a call behind a function pointer";

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

llvm::StringRef beforeDecl(const SourceManager &sm, SourceLocation loc) {
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
  llvm::StringRef chunk = buf.slice(back, offset);
  const size_t brace = chunk.rfind("\n}");
  if (brace != llvm::StringRef::npos) {
    chunk = chunk.substr(brace + 1);
  }
  return chunk;
}

bool fnPtr(QualType type) {
  if (type.isNull() || type->isDependentType()) {
    return false;
  }
  type = type.getNonReferenceType().getCanonicalType();
  if (type->isMemberFunctionPointerType()) {
    return true;
  }
  if (type->isPointerType()) {
    const QualType pointee = type->getPointeeType();
    return pointee->isFunctionType();
  }
  if (const auto *array = type->getAsArrayTypeUnsafe()) {
    return fnPtr(array->getElementType());
  }
  return false;
}

bool arrayOfFnPtr(QualType type) {
  if (type.isNull()) {
    return false;
  }
  type = type.getNonReferenceType();
  return type->isArrayType() && fnPtr(type);
}

llvm::StringRef simpleName(const clang::NamedDecl *decl) {
  if (!decl || !decl->getIdentifier()) {
    return {};
  }
  return decl->getName();
}

bool hookName(llvm::StringRef name) {
  return name.contains_insensitive("task") || name.contains_insensitive("thread") ||
         name.contains_insensitive("hook");
}

bool allowedHere(const SourceManager &sm, const ValueDecl *decl) {
  const llvm::StringRef note = beforeDecl(sm, decl->getBeginLoc());
  if (arrayOfFnPtr(decl->getType()) &&
      hasWord(note, {"vector", "driver", "table", "isr", "irq", "hook"})) {
    return true;
  }
  const auto *parm = llvm::dyn_cast<ParmVarDecl>(decl);
  if (!parm) {
    return false;
  }
  const auto *fn = llvm::dyn_cast<FunctionDecl>(parm->getDeclContext());
  const bool namedHook = fn && hookName(simpleName(fn));
  return (namedHook || hasWord(note, {"hook", "task", "thread", "rtos"})) &&
         hasWord(note, {"hook", "task", "thread", "rtos", "vector", "driver", "table"});
}

bool namedAllow(Reporter &reporter, llvm::StringRef name) {
  if (name.empty()) {
    return false;
  }
  const std::string owned = name.str();
  return reporter.allows(kId, owned.c_str());
}

void emit(Reporter &reporter, const SourceManager &sm, SourceLocation loc, llvm::StringRef name) {
  if (!project(sm, loc) || namedAllow(reporter, name)) {
    return;
  }
  reporter.emit(sm, loc, kId, kMsg);
}

} // namespace

void NoFnPointerCheck::registerMatchers(clang::ast_matchers::MatchFinder &finder) {
  finder.addMatcher(varDecl().bind("var"), this);
  finder.addMatcher(fieldDecl().bind("field"), this);
  finder.addMatcher(functionDecl().bind("fn"), this);
  finder.addMatcher(typedefNameDecl().bind("alias"), this);
}

void NoFnPointerCheck::run(const clang::ast_matchers::MatchFinder::MatchResult &result) {
  if (!result.SourceManager) {
    return;
  }
  const SourceManager &sm = *result.SourceManager;
  if (const auto *var = result.Nodes.getNodeAs<VarDecl>("var")) {
    if (var->isImplicit() || !fnPtr(var->getType()) || allowedHere(sm, var)) {
      return;
    }
    if (const auto *parm = llvm::dyn_cast<ParmVarDecl>(var)) {
      if (const auto *fn = llvm::dyn_cast<FunctionDecl>(parm->getDeclContext())) {
        if (namedAllow(reporter, simpleName(fn))) {
          return;
        }
      }
    }
    emit(reporter, sm, var->getLocation(), simpleName(var));
    return;
  }
  if (const auto *field = result.Nodes.getNodeAs<FieldDecl>("field")) {
    if (field->isImplicit() || !fnPtr(field->getType()) || allowedHere(sm, field)) {
      return;
    }
    emit(reporter, sm, field->getLocation(), simpleName(field));
    return;
  }
  if (const auto *fn = result.Nodes.getNodeAs<FunctionDecl>("fn")) {
    if (fn->isImplicit() || !fnPtr(fn->getReturnType())) {
      return;
    }
    emit(reporter, sm, fn->getLocation(), simpleName(fn));
    return;
  }
  if (const auto *alias = result.Nodes.getNodeAs<TypedefNameDecl>("alias")) {
    if (!fnPtr(alias->getUnderlyingType())) {
      return;
    }
    emit(reporter, sm, alias->getLocation(), simpleName(alias));
  }
}
