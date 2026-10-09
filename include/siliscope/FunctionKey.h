#pragma once

#include "clang/AST/Decl.h"
#include "clang/Basic/Linkage.h"
#include "clang/Basic/SourceManager.h"
#include "clang/UnifiedSymbolResolution/USRGeneration.h"
#include "llvm/ADT/SmallString.h"

#include <string>

// Same key as the call graph: USR, plus the file for an internal-linkage
// function so two statics with one name stay distinct.
inline std::string functionKey(const clang::FunctionDecl *fn, const clang::SourceManager &sm) {
  if (!fn) {
    return {};
  }
  fn = fn->getCanonicalDecl();
  llvm::SmallString<256> buf;
  if (clang::index::generateUSRForDecl(fn, buf) || buf.empty()) {
    return {};
  }
  std::string key(buf);
  if (fn->getFormalLinkage() == clang::Linkage::Internal) {
    const clang::SourceLocation loc = sm.getSpellingLoc(fn->getLocation());
    const clang::PresumedLoc pl = sm.getPresumedLoc(loc);
    if (!pl.isInvalid() && pl.getFilename()) {
      key.push_back('@');
      key.append(pl.getFilename());
    }
  }
  return key;
}
