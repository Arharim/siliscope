#pragma once

#include "clang/AST/Decl.h"
#include "llvm/ADT/StringRef.h"

namespace siliscope {

// Same names CsBalanced and IrqMask already treat as entering or leaving a
// critical section or a saved interrupt mask.
enum class SectionOp { None, Enter, Leave, Restore };

enum class IrqOp {
  None,
  BlindEnable,
  DisableIrq,
  RestorePrimask,
  DisableFault,
  RestoreFault,
  RaiseBasepri,
  SetBasepri
};

inline SectionOp sectionOp(const clang::FunctionDecl *fn) {
  if (!fn || !fn->getIdentifier()) {
    return SectionOp::None;
  }
  const llvm::StringRef n = fn->getName();
  if (n.contains("ENTER_CRITICAL") || n.contains("EnterCritical") || n.ends_with("disable_irq") ||
      n == "__disable_fault_irq" || n == "irq_lock") {
    return SectionOp::Enter;
  }
  if (n.contains("EXIT_CRITICAL") || n.contains("ExitCritical") || n.ends_with("enable_irq") ||
      n == "__enable_fault_irq" || n == "irq_unlock") {
    return SectionOp::Leave;
  }
  if (n == "__set_PRIMASK" || n == "__set_FAULTMASK") {
    return SectionOp::Restore;
  }
  return SectionOp::None;
}

inline IrqOp irqOp(const clang::FunctionDecl *fn) {
  if (!fn || !fn->getIdentifier()) {
    return IrqOp::None;
  }
  const llvm::StringRef n = fn->getName();
  if (n.ends_with("enable_irq") || n == "__enable_fault_irq" || n == "cpsie") {
    return IrqOp::BlindEnable;
  }
  if (n.ends_with("disable_irq") || n == "cpsid") {
    return IrqOp::DisableIrq;
  }
  if (n == "__disable_fault_irq") {
    return IrqOp::DisableFault;
  }
  if (n == "__set_PRIMASK") {
    return IrqOp::RestorePrimask;
  }
  if (n == "__set_FAULTMASK") {
    return IrqOp::RestoreFault;
  }
  if (n == "__set_BASEPRI_MAX") {
    return IrqOp::RaiseBasepri;
  }
  if (n == "__set_BASEPRI") {
    return IrqOp::SetBasepri;
  }
  return IrqOp::None;
}

} // namespace siliscope
