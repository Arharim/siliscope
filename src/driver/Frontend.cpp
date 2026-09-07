#include "siliscope/Frontend.h"

#include "siliscope/Braces.h"
#include "siliscope/NoGoto.h"
#include "siliscope/NoHeap.h"
#include "siliscope/NoSetjmp.h"
#include "siliscope/NoStdio.h"
#include "siliscope/NoUnboundedString.h"
#include "siliscope/Report.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Attr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/JSONCompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
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
  AnalyzeAction(Reporter &reporter, Probe *probe)
      : probe(probe),
        no_goto(reporter),
        no_setjmp(reporter),
        no_heap(reporter),
        no_unbounded(reporter),
        no_stdio(reporter),
        braces(reporter) {
    no_goto.registerMatchers(finder);
    no_setjmp.registerMatchers(finder);
    no_heap.registerMatchers(finder);
    no_unbounded.registerMatchers(finder);
    no_stdio.registerMatchers(finder);
    braces.registerMatchers(finder);
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
};

class AnalyzeFactory : public FrontendActionFactory {
public:
  AnalyzeFactory(Reporter &reporter, Probe *probe) : reporter(reporter), probe(probe) {}

  std::unique_ptr<FrontendAction> create() override {
    return std::make_unique<AnalyzeAction>(reporter, probe);
  }

private:
  Reporter &reporter;
  Probe *probe;
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

} // namespace

int runFrontend(const FrontendOptions &opt) {
  std::string err;
  auto db = loadCompilations(opt, err);
  if (!db) {
    llvm::errs() << "error: " << err << "\n";
    return 1;
  }

  Probe probe;
  Reporter reporter;
  ClangTool tool(*db, opt.sources);
  AnalyzeFactory factory(reporter, opt.probe ? &probe : nullptr);
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
