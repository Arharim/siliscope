#pragma once

#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "llvm/ADT/StringRef.h"

inline bool isIsr(const clang::FunctionDecl *fn) {
  if (!fn) {
    return false;
  }
  fn = fn->getCanonicalDecl();
  if (fn->hasAttrs()) {
    for (const auto *a : fn->attrs()) {
      if (!a) {
        continue;
      }
      const llvm::StringRef sp = a->getSpelling();
      if (sp.contains_insensitive("interrupt")) {
        return true;
      }
    }
  }
  if (!fn->getIdentifier()) {
    return false;
  }
  const llvm::StringRef n = fn->getName();
  if (n.ends_with("_IRQHandler")) {
    return true;
  }
  // A helper such as ringbuf_push_isr(buf, byte) is not a vector entry.
  // Hardware handlers named *_isr take no parameters and return void.
  return n.ends_with("_isr") && fn->getNumParams() == 0 && fn->getReturnType()->isVoidType();
}
