#include "siliscope/Registry.h"

#include "siliscope/Braces.h"
#include "siliscope/Check.h"
#include "siliscope/CheckReturn.h"
#include "siliscope/CsBalanced.h"
#include "siliscope/DeclConst.h"
#include "siliscope/Distinct.h"
#include "siliscope/IfElseFinal.h"
#include "siliscope/IrqMaskBalanced.h"
#include "siliscope/IsrNoFp.h"
#include "siliscope/IsrNotCalled.h"
#include "siliscope/NoAbortSystem.h"
#include "siliscope/NoAssignInCond.h"
#include "siliscope/NoAtoi.h"
#include "siliscope/NoBlockScope.h"
#include "siliscope/NoComma.h"
#include "siliscope/NoContinue.h"
#include "siliscope/NoFlexibleArray.h"
#include "siliscope/NoGoto.h"
#include "siliscope/NoHeap.h"
#include "siliscope/NoIncInExpr.h"
#include "siliscope/NoLogInIsr.h"
#include "siliscope/NoLogicalRhs.h"
#include "siliscope/NoNestedTernary.h"
#include "siliscope/NoOctal.h"
#include "siliscope/NoQsort.h"
#include "siliscope/NoRand.h"
#include "siliscope/NoRecursion.h"
#include "siliscope/NoSetjmp.h"
#include "siliscope/NoSetlocale.h"
#include "siliscope/NoShadow.h"
#include "siliscope/NoSignal.h"
#include "siliscope/NoSizeofSideEffect.h"
#include "siliscope/NoStdarg.h"
#include "siliscope/NoStdio.h"
#include "siliscope/NoUnboundedString.h"
#include "siliscope/NoUnusedParams.h"
#include "siliscope/NoVLA.h"
#include "siliscope/Noreturn.h"
#include "siliscope/Prototype.h"
#include "siliscope/PtrNull.h"
#include "siliscope/ReturnAllPaths.h"
#include "siliscope/StaticInternal.h"
#include "siliscope/StringConst.h"
#include "siliscope/Unreachable.h"

#include <cstring>

template <typename T>
static std::unique_ptr<Check> makeCheck(Reporter &reporter) {
  return std::make_unique<T>(reporter);
}

static const CheckSpec kSpecs[] = {
    {"ss.ctrl.braces", makeCheck<BracesCheck>},
    {"ss.ctrl.if-else-final", makeCheck<IfElseFinalCheck>},
    {"ss.ctrl.no-assignment-in-condition", makeCheck<NoAssignInCondCheck>},
    {"ss.ctrl.no-continue", makeCheck<NoContinueCheck>},
    {"ss.ctrl.no-goto", makeCheck<NoGotoCheck>},
    {"ss.ctrl.no-nested-ternary", makeCheck<NoNestedTernaryCheck>},
    {"ss.ctrl.no-recursion", makeCheck<NoRecursionCheck>},
    {"ss.ctrl.no-setjmp", makeCheck<NoSetjmpCheck>},
    {"ss.ctrl.unreachable", makeCheck<UnreachableCheck>},
    {"ss.decl.const", makeCheck<DeclConstCheck>},
    {"ss.decl.distinct", makeCheck<DistinctCheck>},
    {"ss.decl.no-shadow", makeCheck<NoShadowCheck>},
    {"ss.decl.ptr-null", makeCheck<PtrNullCheck>},
    {"ss.emb.cs-balanced", makeCheck<CsBalancedCheck>},
    {"ss.emb.irq-mask-balanced", makeCheck<IrqMaskBalancedCheck>},
    {"ss.emb.isr-no-fp", makeCheck<IsrNoFpCheck>},
    {"ss.emb.isr-not-called", makeCheck<IsrNotCalledCheck>},
    {"ss.emb.no-log-in-isr", makeCheck<NoLogInIsrCheck>},
    {"ss.expr.no-comma", makeCheck<NoCommaCheck>},
    {"ss.expr.no-inc-in-expr", makeCheck<NoIncInExprCheck>},
    {"ss.expr.no-logical-rhs-side-effect", makeCheck<NoLogicalRhsCheck>},
    {"ss.expr.no-octal", makeCheck<NoOctalCheck>},
    {"ss.expr.no-sizeof-side-effect", makeCheck<NoSizeofSideEffectCheck>},
    {"ss.expr.string-const", makeCheck<StringConstCheck>},
    {"ss.fn.check-return", makeCheck<CheckReturnCheck>},
    {"ss.fn.no-block-scope", makeCheck<NoBlockScopeCheck>},
    {"ss.fn.no-stdarg", makeCheck<NoStdargCheck>},
    {"ss.fn.no-unused-params", makeCheck<NoUnusedParamsCheck>},
    {"ss.fn.noreturn-does-not-return", makeCheck<NoreturnCheck>},
    {"ss.fn.prototype", makeCheck<PrototypeCheck>},
    {"ss.fn.return-all-paths", makeCheck<ReturnAllPathsCheck>},
    {"ss.fn.static-internal", makeCheck<StaticInternalCheck>},
    {"ss.libc.no-abort-system", makeCheck<NoAbortSystemCheck>},
    {"ss.libc.no-atoi", makeCheck<NoAtoiCheck>},
    {"ss.libc.no-qsort-bsearch", makeCheck<NoQsortCheck>},
    {"ss.libc.no-rand", makeCheck<NoRandCheck>},
    {"ss.libc.no-setlocale", makeCheck<NoSetlocaleCheck>},
    {"ss.libc.no-signal", makeCheck<NoSignalCheck>},
    {"ss.libc.no-stdio", makeCheck<NoStdioCheck>},
    {"ss.libc.no-unbounded-string", makeCheck<NoUnboundedStringCheck>},
    {"ss.mem.no-flexible-array", makeCheck<NoFlexibleArrayCheck>},
    {"ss.mem.no-heap-after-init", makeCheck<NoHeapCheck>},
    {"ss.mem.no-vla", makeCheck<NoVLACheck>},
};

const CheckSpec *checkSpecs() {
  return kSpecs;
}

unsigned checkSpecCount() {
  return sizeof(kSpecs) / sizeof(kSpecs[0]);
}

bool hasChecker(const char *id) {
  if (!id) {
    return false;
  }
  for (const CheckSpec &spec : kSpecs) {
    if (std::strcmp(spec.id, id) == 0) {
      return true;
    }
  }
  return false;
}
