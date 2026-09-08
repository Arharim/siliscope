#include "siliscope/Frontend.h"

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
#include "siliscope/Profile.h"
#include "siliscope/Prototype.h"
#include "siliscope/PtrNull.h"
#include "siliscope/Report.h"
#include "siliscope/ReturnAllPaths.h"
#include "siliscope/StaticInternal.h"
#include "siliscope/StringConst.h"
#include "siliscope/Unreachable.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Attr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/JSONCompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using clang::ASTConsumer;
using clang::ASTContext;
using clang::ASTFrontendAction;
using clang::CompilerInstance;
using clang::FrontendAction;
using clang::FunctionDecl;
using clang::PackedAttr;
using clang::RecordDecl;
using clang::ast_matchers::MatchFinder;
using clang::tooling::ClangTool;
using clang::tooling::CommandLineArguments;
using clang::tooling::CompilationDatabase;
using clang::tooling::FixedCompilationDatabase;
using clang::tooling::FrontendActionFactory;
using clang::tooling::JSONCompilationDatabase;

namespace {

struct Probe {
  unsigned functions = 0;
  unsigned interrupt = 0;
  unsigned packed = 0;
};

class ProbeVisitor : public clang::RecursiveASTVisitor<ProbeVisitor> {
public:
  explicit ProbeVisitor(Probe &p) : p(p) {}

  bool VisitFunctionDecl(FunctionDecl *d) {
    if (!d || !d->isThisDeclarationADefinition() || d->isImplicit()) {
      return true;
    }
    ++p.functions;
    if (d->hasAttrs()) {
      for (const auto *a : d->attrs()) {
        if (!a) {
          continue;
        }
        llvm::StringRef sp = a->getSpelling();
        if (sp.contains_insensitive("interrupt")) {
          ++p.interrupt;
        }
      }
    }
    return true;
  }

  bool VisitRecordDecl(RecordDecl *d) {
    if (!d || !d->isCompleteDefinition() || d->isImplicit()) {
      return true;
    }
    if (d->hasAttr<PackedAttr>()) {
      ++p.packed;
    }
    return true;
  }

private:
  Probe &p;
};

class ProbeConsumer : public ASTConsumer {
public:
  explicit ProbeConsumer(Probe &p) : p(p) {}

  void HandleTranslationUnit(ASTContext &ctx) override {
    ProbeVisitor v(p);
    v.TraverseDecl(ctx.getTranslationUnitDecl());
  }

private:
  Probe &p;
};

class AnalyzeAction : public ASTFrontendAction {
public:
  AnalyzeAction(Reporter &reporter, Probe *probe, const Profile &profile)
      : probe(probe),
        no_goto(reporter),
        no_setjmp(reporter),
        no_heap(reporter),
        no_unbounded(reporter),
        no_stdio(reporter),
        braces(reporter),
        no_assign(reporter),
        no_octal(reporter),
        no_vla(reporter),
        no_stdarg(reporter),
        no_signal(reporter),
        no_atoi(reporter),
        no_abort(reporter),
        no_qsort(reporter),
        no_rand(reporter),
        no_setlocale(reporter),
        no_comma(reporter),
        no_flexarray(reporter),
        no_blockscope(reporter),
        if_else_final(reporter),
        no_continue(reporter),
        no_nested_ternary(reporter),
        no_inc(reporter),
        no_sizeof_se(reporter),
        no_logical_rhs(reporter),
        prototype(reporter),
        return_all_paths(reporter),
        unreachable(reporter),
        noreturn_fn(reporter),
        no_recursion(reporter),
        isr_not_called(reporter),
        no_log_in_isr(reporter),
        cs_balanced(reporter),
        irq_mask_balanced(reporter),
        isr_no_fp(reporter),
        check_return(reporter),
        no_shadow(reporter),
        string_const(reporter),
        ptr_null(reporter),
        distinct(reporter),
        decl_const(reporter),
        static_internal(reporter),
        no_unused_params(reporter) {
    Check *const all[] = {&no_goto,           &no_setjmp,     &no_heap,
                          &no_unbounded,      &no_stdio,      &braces,
                          &no_assign,         &no_octal,      &no_vla,
                          &no_stdarg,         &no_signal,     &no_atoi,
                          &no_abort,          &no_qsort,      &no_rand,
                          &no_setlocale,      &no_comma,      &no_flexarray,
                          &no_blockscope,     &if_else_final, &no_continue,
                          &no_nested_ternary, &no_inc,        &no_sizeof_se,
                          &no_logical_rhs,    &prototype,     &return_all_paths,
                          &unreachable,       &noreturn_fn,   &no_recursion,
                          &isr_not_called,    &no_log_in_isr, &cs_balanced,
                          &irq_mask_balanced, &isr_no_fp,     &check_return,
                          &no_shadow,         &string_const,  &ptr_null,
                          &distinct,          &decl_const,    &static_internal,
                          &no_unused_params};
    const char *const ids[] = {"ss.ctrl.no-goto",
                               "ss.ctrl.no-setjmp",
                               "ss.mem.no-heap-after-init",
                               "ss.libc.no-unbounded-string",
                               "ss.libc.no-stdio",
                               "ss.ctrl.braces",
                               "ss.ctrl.no-assignment-in-condition",
                               "ss.expr.no-octal",
                               "ss.mem.no-vla",
                               "ss.fn.no-stdarg",
                               "ss.libc.no-signal",
                               "ss.libc.no-atoi",
                               "ss.libc.no-abort-system",
                               "ss.libc.no-qsort-bsearch",
                               "ss.libc.no-rand",
                               "ss.libc.no-setlocale",
                               "ss.expr.no-comma",
                               "ss.mem.no-flexible-array",
                               "ss.fn.no-block-scope",
                               "ss.ctrl.if-else-final",
                               "ss.ctrl.no-continue",
                               "ss.ctrl.no-nested-ternary",
                               "ss.expr.no-inc-in-expr",
                               "ss.expr.no-sizeof-side-effect",
                               "ss.expr.no-logical-rhs-side-effect",
                               "ss.fn.prototype",
                               "ss.fn.return-all-paths",
                               "ss.ctrl.unreachable",
                               "ss.fn.noreturn-does-not-return",
                               "ss.ctrl.no-recursion",
                               "ss.emb.isr-not-called",
                               "ss.emb.no-log-in-isr",
                               "ss.emb.cs-balanced",
                               "ss.emb.irq-mask-balanced",
                               "ss.emb.isr-no-fp",
                               "ss.fn.check-return",
                               "ss.decl.no-shadow",
                               "ss.expr.string-const",
                               "ss.decl.ptr-null",
                               "ss.decl.distinct",
                               "ss.decl.const",
                               "ss.fn.static-internal",
                               "ss.fn.no-unused-params"};
    static_assert(sizeof(all) / sizeof(all[0]) == sizeof(ids) / sizeof(ids[0]));
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
      if (profile.isEnabled(ids[i])) {
        all[i]->registerMatchers(finder);
      }
    }
  }

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &, llvm::StringRef) override {
    std::vector<std::unique_ptr<ASTConsumer>> cs;
    cs.push_back(finder.newASTConsumer());
    if (probe) {
      cs.push_back(std::make_unique<ProbeConsumer>(*probe));
    }
    return std::make_unique<clang::MultiplexConsumer>(std::move(cs));
  }

private:
  Probe *probe;
  MatchFinder finder;
  NoGotoCheck no_goto;
  NoSetjmpCheck no_setjmp;
  NoHeapCheck no_heap;
  NoUnboundedStringCheck no_unbounded;
  NoStdioCheck no_stdio;
  BracesCheck braces;
  NoAssignInCondCheck no_assign;
  NoOctalCheck no_octal;
  NoVLACheck no_vla;
  NoStdargCheck no_stdarg;
  NoSignalCheck no_signal;
  NoAtoiCheck no_atoi;
  NoAbortSystemCheck no_abort;
  NoQsortCheck no_qsort;
  NoRandCheck no_rand;
  NoSetlocaleCheck no_setlocale;
  NoCommaCheck no_comma;
  NoFlexibleArrayCheck no_flexarray;
  NoBlockScopeCheck no_blockscope;
  IfElseFinalCheck if_else_final;
  NoContinueCheck no_continue;
  NoNestedTernaryCheck no_nested_ternary;
  NoIncInExprCheck no_inc;
  NoSizeofSideEffectCheck no_sizeof_se;
  NoLogicalRhsCheck no_logical_rhs;
  PrototypeCheck prototype;
  ReturnAllPathsCheck return_all_paths;
  UnreachableCheck unreachable;
  NoreturnCheck noreturn_fn;
  NoRecursionCheck no_recursion;
  IsrNotCalledCheck isr_not_called;
  NoLogInIsrCheck no_log_in_isr;
  CsBalancedCheck cs_balanced;
  IrqMaskBalancedCheck irq_mask_balanced;
  IsrNoFpCheck isr_no_fp;
  CheckReturnCheck check_return;
  NoShadowCheck no_shadow;
  StringConstCheck string_const;
  PtrNullCheck ptr_null;
  DistinctCheck distinct;
  DeclConstCheck decl_const;
  StaticInternalCheck static_internal;
  NoUnusedParamsCheck no_unused_params;
};

class AnalyzeFactory : public FrontendActionFactory {
public:
  AnalyzeFactory(Reporter &reporter, Probe *probe, const Profile &profile)
      : reporter(reporter), probe(probe), profile(profile) {}

  std::unique_ptr<FrontendAction> create() override {
    return std::make_unique<AnalyzeAction>(reporter, probe, profile);
  }

private:
  Reporter &reporter;
  Probe *probe;
  const Profile &profile;
};

std::unique_ptr<CompilationDatabase> loadCompilations(const FrontendOptions &opt,
                                                      std::string &err) {
  if (!opt.compile_commands_dir.empty()) {
    auto db = JSONCompilationDatabase::loadFromDirectory(opt.compile_commands_dir, err);
    if (db) {
      return db;
    }
    err = "compile_commands.json: " + err;
    return nullptr;
  }

  std::vector<std::string> cmd = {
      "-fsyntax-only",
      "-fgnuc-version=12.0.0",
      "--target=" + opt.target,
  };
  cmd.insert(cmd.end(), opt.extra_args.begin(), opt.extra_args.end());
  return std::make_unique<FixedCompilationDatabase>(".", cmd);
}

// GNU arm-none-eabi-gcc lives in <prefix>/bin; headers are <prefix>/arm-none-eabi.
static llvm::SmallString<256> gnuArmPrefix(llvm::StringRef compiler) {
  llvm::SmallString<256> path(compiler);
  llvm::sys::path::remove_filename(path);
  llvm::sys::path::remove_filename(path);
  return path;
}

static std::string gnuArmSysroot(llvm::StringRef compiler) {
  llvm::SmallString<256> path = gnuArmPrefix(compiler);
  llvm::sys::path::append(path, "arm-none-eabi");
  if (llvm::sys::fs::is_directory(path)) {
    return std::string(path);
  }
  return {};
}

// lib/gcc/arm-none-eabi/<ver>/include holds stddef.h / stdarg.h for the cross gcc.
static std::string gnuArmGccInclude(llvm::StringRef compiler) {
  llvm::SmallString<256> dir = gnuArmPrefix(compiler);
  llvm::sys::path::append(dir, "lib", "gcc", "arm-none-eabi");
  std::error_code ec;
  for (llvm::sys::fs::directory_iterator it(dir, ec), e; it != e && !ec; it.increment(ec)) {
    llvm::SmallString<256> inc(it->path());
    llvm::sys::path::append(inc, "include");
    if (llvm::sys::fs::is_directory(inc)) {
      return std::string(inc);
    }
  }
  return {};
}

} // namespace

int runFrontend(const FrontendOptions &opt) {
  std::string err;
  Profile profile;
  if (!loadProfile(opt.ruleset_dir, opt.profile, profile, err)) {
    llvm::errs() << "error: " << err << "\n";
    return 1;
  }
  for (const auto &a : opt.allow) {
    profile.addAllow(a.first, a.second);
  }

  auto db = loadCompilations(opt, err);
  if (!db) {
    llvm::errs() << "error: " << err << "\n";
    return 1;
  }

  Probe probe;
  Reporter reporter(profile);
  ClangTool tool(*db, opt.sources);
  if (!opt.compile_commands_dir.empty()) {
    const std::string tgt = "--target=" + opt.target;
    const std::vector<std::string> extras = opt.extra_args;
    tool.appendArgumentsAdjuster([tgt, extras](const CommandLineArguments &args, llvm::StringRef) {
      CommandLineArguments out;
      if (!args.empty()) {
        out.push_back(args.front());
        const llvm::StringRef compiler = args.front();
        if (compiler.contains_insensitive("arm-none-eabi-gcc") ||
            compiler.contains_insensitive("arm-none-eabi-g++")) {
          out.push_back(tgt);
          const std::string sys = gnuArmSysroot(compiler);
          if (!sys.empty()) {
            out.push_back("--sysroot=" + sys);
          }
          const std::string gccinc = gnuArmGccInclude(compiler);
          if (!gccinc.empty()) {
            out.push_back("-isystem");
            out.push_back(gccinc);
          }
        }
      }
      for (size_t i = 1; i < args.size(); ++i) {
        if (llvm::StringRef(args[i]).starts_with("--specs=")) {
          continue;
        }
        out.push_back(args[i]);
      }
      out.insert(out.end(), extras.begin(), extras.end());
      return out;
    });
  }
  AnalyzeFactory factory(reporter, opt.probe ? &probe : nullptr, profile);
  const int parse_rc = tool.run(&factory);

  if (opt.probe) {
    llvm::outs() << "target: " << opt.target << "\n"
                 << "files: " << opt.sources.size() << "\n"
                 << "functions: " << probe.functions << "\n"
                 << "interrupt: " << probe.interrupt << "\n"
                 << "packed: " << probe.packed << "\n";
  }
  reporter.printSummary();

  if (parse_rc != 0) {
    return parse_rc;
  }
  return reporter.count() ? 1 : 0;
}
