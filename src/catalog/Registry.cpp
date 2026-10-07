#include "siliscope/Registry.h"

#include "siliscope/Braces.h"
#include "siliscope/Check.h"
#include "siliscope/CheckReturn.h"
#include "siliscope/CsBalanced.h"
#include "siliscope/DeclConst.h"
#include "siliscope/Distinct.h"
#include "siliscope/EmptyLoopComment.h"
#include "siliscope/EnumClass.h"
#include "siliscope/IfElseFinal.h"
#include "siliscope/InitMembers.h"
#include "siliscope/IrqMaskBalanced.h"
#include "siliscope/IsrNoFp.h"
#include "siliscope/IsrNotCalled.h"
#include "siliscope/NoAbortSystem.h"
#include "siliscope/NoAssignInCond.h"
#include "siliscope/NoAtoi.h"
#include "siliscope/NoBlockScope.h"
#include "siliscope/NoCStyleCast.h"
#include "siliscope/NoComma.h"
#include "siliscope/NoContinue.h"
#include "siliscope/NoCvAway.h"
#include "siliscope/NoDefaultArgs.h"
#include "siliscope/NoExceptions.h"
#include "siliscope/NoFlexibleArray.h"
#include "siliscope/NoFpEq.h"
#include "siliscope/NoFriend.h"
#include "siliscope/NoGoto.h"
#include "siliscope/NoHeap.h"
#include "siliscope/NoHeapStl.h"
#include "siliscope/NoImplicitConversion.h"
#include "siliscope/NoIncInExpr.h"
#include "siliscope/NoLogInIsr.h"
#include "siliscope/NoLogicalRhs.h"
#include "siliscope/NoMoveConst.h"
#include "siliscope/NoNestedTernary.h"
#include "siliscope/NoOctal.h"
#include "siliscope/NoPtrInt.h"
#include "siliscope/NoQsort.h"
#include "siliscope/NoRand.h"
#include "siliscope/NoRecursion.h"
#include "siliscope/NoRtti.h"
#include "siliscope/NoSetjmp.h"
#include "siliscope/NoSetlocale.h"
#include "siliscope/NoShadow.h"
#include "siliscope/NoSignal.h"
#include "siliscope/NoSignedBitwise.h"
#include "siliscope/NoSilentNarrow.h"
#include "siliscope/NoSizeofSideEffect.h"
#include "siliscope/NoStdarg.h"
#include "siliscope/NoStdio.h"
#include "siliscope/NoThrowDtor.h"
#include "siliscope/NoThrowSpec.h"
#include "siliscope/NoThrowingSwap.h"
#include "siliscope/NoUnboundedString.h"
#include "siliscope/NoUnusedParams.h"
#include "siliscope/NoUsingDirective.h"
#include "siliscope/NoUsingInHeader.h"
#include "siliscope/NoVLA.h"
#include "siliscope/NoVectorBool.h"
#include "siliscope/Noreturn.h"
#include "siliscope/Nullptr.h"
#include "siliscope/Override.h"
#include "siliscope/PreprocessorCheck.h"
#include "siliscope/PrivateData.h"
#include "siliscope/Prototype.h"
#include "siliscope/PtrNull.h"
#include "siliscope/ReturnAllPaths.h"
#include "siliscope/ShiftRange.h"
#include "siliscope/SpecialMembers.h"
#include "siliscope/StaticInternal.h"
#include "siliscope/StringConst.h"
#include "siliscope/SwitchFallthroughComment.h"
#include "siliscope/SwitchWellFormed.h"
#include "siliscope/USuffix.h"
#include "siliscope/Unreachable.h"
#include "siliscope/VirtualDtor.h"

#include <cstring>

template <typename T>
static std::unique_ptr<Check> makeCheck(Reporter &reporter) {
  return std::make_unique<T>(reporter);
}

static const CheckSpec kSpecs[] = {
    {"ss.conv.no-cv-away", makeCheck<NoCvAwayCheck>},
    {"ss.conv.no-fp-eq", makeCheck<NoFpEqCheck>},
    {"ss.conv.no-ptr-int", makeCheck<NoPtrIntCheck>},
    {"ss.conv.no-signed-bitwise", makeCheck<NoSignedBitwiseCheck>},
    {"ss.conv.no-silent-narrow", makeCheck<NoSilentNarrowCheck>},
    {"ss.conv.shift-range", makeCheck<ShiftRangeCheck>},
    {"ss.conv.u-suffix", makeCheck<USuffixCheck>},
    {"ss.cpp.enum-class", makeCheck<EnumClassCheck>},
    {"ss.cpp.init-members", makeCheck<InitMembersCheck>},
    {"ss.cpp.no-cstyle-cast", makeCheck<NoCStyleCastCheck>},
    {"ss.cpp.no-default-args", makeCheck<NoDefaultArgsCheck>},
    {"ss.cpp.no-exceptions", makeCheck<NoExceptionsCheck>},
    {"ss.cpp.no-friend", makeCheck<NoFriendCheck>},
    {"ss.cpp.no-heap-stl", makeCheck<NoHeapStlCheck>},
    {"ss.cpp.no-implicit-conversion", makeCheck<NoImplicitConversionCheck>},
    {"ss.cpp.no-move-const", makeCheck<NoMoveConstCheck>},
    {"ss.cpp.no-rtti", makeCheck<NoRttiCheck>},
    {"ss.cpp.no-throw-dtor", makeCheck<NoThrowDtorCheck>},
    {"ss.cpp.no-throw-spec", makeCheck<NoThrowSpecCheck>},
    {"ss.cpp.no-throwing-swap", makeCheck<NoThrowingSwapCheck>},
    {"ss.cpp.no-using-directive", makeCheck<NoUsingDirectiveCheck>},
    {"ss.cpp.no-using-in-header", makeCheck<NoUsingInHeaderCheck>},
    {"ss.cpp.no-vector-bool", makeCheck<NoVectorBoolCheck>},
    {"ss.cpp.nullptr", makeCheck<NullptrCheck>},
    {"ss.cpp.override", makeCheck<OverrideCheck>},
    {"ss.cpp.private-data", makeCheck<PrivateDataCheck>},
    {"ss.cpp.special-members", makeCheck<SpecialMembersCheck>},
    {"ss.cpp.virtual-dtor", makeCheck<VirtualDtorCheck>},
    {"ss.ctrl.braces", makeCheck<BracesCheck>},
    {"ss.ctrl.empty-loop-comment", makeCheck<EmptyLoopCommentCheck>},
    {"ss.ctrl.if-else-final", makeCheck<IfElseFinalCheck>},
    {"ss.ctrl.no-assignment-in-condition", makeCheck<NoAssignInCondCheck>},
    {"ss.ctrl.no-continue", makeCheck<NoContinueCheck>},
    {"ss.ctrl.no-goto", makeCheck<NoGotoCheck>},
    {"ss.ctrl.no-nested-ternary", makeCheck<NoNestedTernaryCheck>},
    {"ss.ctrl.no-recursion", makeCheck<NoRecursionCheck>},
    {"ss.ctrl.no-setjmp", makeCheck<NoSetjmpCheck>},
    {"ss.ctrl.switch-fallthrough-comment", makeCheck<SwitchFallthroughCommentCheck>},
    {"ss.ctrl.switch-well-formed", makeCheck<SwitchWellFormedCheck>},
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
    {"ss.pre.comment-tokens", makeCheck<PreprocessorCheck>},
    {"ss.pre.ifdef-same-file", makeCheck<PreprocessorCheck>},
    {"ss.pre.include-guard", makeCheck<PreprocessorCheck>},
    {"ss.pre.limited", makeCheck<PreprocessorCheck>},
    {"ss.pre.macro-parens", makeCheck<PreprocessorCheck>},
    {"ss.pre.no-commented-code", makeCheck<PreprocessorCheck>},
    {"ss.pre.no-keyword-macro", makeCheck<PreprocessorCheck>},
    {"ss.pre.no-path-in-include", makeCheck<PreprocessorCheck>},
    {"ss.pre.no-stringify-then-paste", makeCheck<PreprocessorCheck>},
    {"ss.pre.no-unused-include", makeCheck<PreprocessorCheck>},
    {"ss.pre.prefer-inline", makeCheck<PreprocessorCheck>},
    {"ss.pre.source-includes-own-header", makeCheck<PreprocessorCheck>},
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
