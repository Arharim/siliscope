#include "siliscope/Frontend.h"

#include "siliscope/Check.h"
#include "siliscope/PreprocessorCheck.h"
#include "siliscope/Profile.h"
#include "siliscope/Registry.h"
#include "siliscope/Report.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Attr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/Version.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Options/OptionUtils.h"
#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/JSONCompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

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
      : reporter(reporter), probe(probe) {
    const CheckSpec *specs = checkSpecs();
    for (unsigned i = 0; i < checkSpecCount(); ++i) {
      if (!profile.isEnabled(specs[i].id)) {
        continue;
      }
      checks.push_back(specs[i].make(reporter));
      checks.back()->registerMatchers(finder);
      if (checks.back()->wantsPreprocessor()) {
        watchPreprocessor = true;
      }
    }
  }

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &ci, llvm::StringRef) override {
    if (watchPreprocessor) {
      attachPreprocessorPass(ci.getPreprocessor(), reporter);
    }
    std::vector<std::unique_ptr<ASTConsumer>> cs;
    cs.push_back(finder.newASTConsumer());
    if (probe) {
      cs.push_back(std::make_unique<ProbeConsumer>(*probe));
    }
    return std::make_unique<clang::MultiplexConsumer>(std::move(cs));
  }

private:
  Reporter &reporter;
  Probe *probe;
  bool watchPreprocessor = false;
  std::vector<std::unique_ptr<Check>> checks;
  MatchFinder finder;
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

static std::string gnuArmGccInclude(llvm::StringRef compiler);

// GCC adds libstdc++ implicitly. Clang does not, so <cstdint> is missing
// unless these directories are passed through. The second one depends on
// -mcpu/-mfpu/-mfloat-abi; g++ -print-multi-directory names it.
static std::string captureProgram(llvm::StringRef program, llvm::ArrayRef<llvm::StringRef> args) {
  llvm::SmallString<128> outPath;
  int fd = -1;
  if (llvm::sys::fs::createTemporaryFile("siliscope", "txt", fd, outPath)) {
    return {};
  }
  close(fd);
  const std::optional<llvm::StringRef> redirects[3] = {
      std::nullopt, llvm::StringRef(outPath), std::nullopt};
  std::string err;
  const int rc = llvm::sys::ExecuteAndWait(program, args, std::nullopt, redirects, 5, 0, &err);
  std::string text;
  if (rc == 0) {
    if (auto buf = llvm::MemoryBuffer::getFile(outPath)) {
      text = (*buf)->getBuffer().trim().str();
    }
  }
  llvm::sys::fs::remove(outPath);
  return text;
}

static bool affectsMultilib(llvm::StringRef arg) {
  return arg == "-mthumb" || arg == "-marm" || arg == "-mlittle-endian" || arg == "-mbig-endian" ||
         arg.starts_with("-mcpu=") || arg.starts_with("-mfpu=") ||
         arg.starts_with("-mfloat-abi=") || arg.starts_with("-march=");
}

static void appendGnuArmCxxIncludes(clang::tooling::CommandLineArguments &out,
                                    llvm::StringRef compiler,
                                    const clang::tooling::CommandLineArguments &args) {
  if (!compiler.contains_insensitive("g++")) {
    return;
  }
  for (size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "-nostdinc" || args[i] == "-nostdinc++") {
      return;
    }
  }
  const std::string gccinc = gnuArmGccInclude(compiler);
  if (gccinc.empty()) {
    return;
  }
  llvm::SmallString<256> versionDir(gccinc);
  llvm::sys::path::remove_filename(versionDir);
  const llvm::StringRef version = llvm::sys::path::filename(versionDir);
  llvm::SmallString<256> root = gnuArmPrefix(compiler);
  llvm::sys::path::append(root, "arm-none-eabi", "include", "c++", version);
  llvm::SmallString<256> cstdint(root);
  llvm::sys::path::append(cstdint, "cstdint");
  if (!llvm::sys::fs::is_regular_file(cstdint)) {
    return;
  }

  auto add = [&out](const llvm::SmallString<256> &dir) {
    if (!llvm::sys::fs::is_directory(dir)) {
      return;
    }
    out.push_back("-isystem");
    out.push_back(std::string(dir));
  };
  add(root);

  std::vector<llvm::StringRef> argv;
  argv.push_back(compiler);
  for (size_t i = 1; i < args.size(); ++i) {
    if (affectsMultilib(args[i])) {
      argv.push_back(args[i]);
    }
  }
  argv.push_back("-print-multi-directory");
  const std::string multi = captureProgram(compiler, argv);
  llvm::SmallString<256> bits(root);
  llvm::sys::path::append(bits, "arm-none-eabi");
  if (!multi.empty() && multi != ".") {
    llvm::sys::path::append(bits, multi);
  }
  add(bits);

  llvm::SmallString<256> backward(root);
  llvm::sys::path::append(backward, "backward");
  add(backward);
}

// lib/gcc/arm-none-eabi/<ver>/include holds GCC's builtins. It is added with
// -idirafter so Clang's own arm_acle.h and stddef.h stay in front.
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

// GCC flags Clang 22 does not implement. Unknown arguments are errors.
// The others are warnings, and Zephyr's -Werror promotes them.
static bool dropGccArg(llvm::StringRef arg) {
  return arg.starts_with("--specs=") || arg == "-fno-printf-return-value" ||
         arg == "-fno-reorder-functions" || arg == "-fno-defer-pop" || arg == "-fsignaling-nans" ||
         arg == "--param=min-pagesize=0" || arg.starts_with("-mfp16-format=");
}

// <prefix>/lib/gcc/arm-none-eabi/<ver>/include. CMSIS includes <arm_acle.h>,
// and GCC's copy passes runtime arguments to __builtin_arm_*.
static bool isGccBuiltinInclude(llvm::StringRef path) {
  while (path.size() > 1 && (path.ends_with("/") || path.ends_with("\\"))) {
    path = path.drop_back();
  }
  if (llvm::sys::path::filename(path) != "include") {
    return false;
  }
  llvm::SmallString<256> dir(path);
  llvm::sys::path::remove_filename(dir);
  llvm::sys::path::remove_filename(dir);
  if (llvm::sys::path::filename(dir) != "arm-none-eabi") {
    return false;
  }
  llvm::sys::path::remove_filename(dir);
  return llvm::sys::path::filename(dir) == "gcc";
}

// ClangTool derives the resource directory from siliscope's path, which has
// no builtin headers. <arm_acle.h> then falls through to GCC's copy.
// Zephyr also passes -nostdinc, so the resource directory is not searched
// unless this path is added with -isystem in front of GCC's include.
static std::string clangResourceDir() {
  auto usable = [](llvm::StringRef dir) {
    llvm::SmallString<256> inc(dir);
    llvm::sys::path::append(inc, "include", "stddef.h");
    return llvm::sys::fs::is_regular_file(inc);
  };
  if (llvm::ErrorOr<std::string> clang = llvm::sys::findProgramByName("clang")) {
    const std::string dir = clang::GetResourcesPath(*clang);
    if (usable(dir)) {
      return dir;
    }
  }
  const std::string major = CLANG_VERSION_MAJOR_STRING;
  const char *roots[] = {"/usr/lib/clang/", "/usr/lib64/clang/"};
  for (const char *root : roots) {
    const std::string dir = std::string(root) + major;
    if (usable(dir)) {
      return dir;
    }
  }
  return {};
}

static unsigned countMissingCheckers(const Profile &profile) {
  unsigned missing = 0;
  for (const auto &kv : profile.enabled) {
    if (!hasChecker(kv.first.c_str())) {
      ++missing;
    }
  }
  return missing;
}

static int printRuleList(const Profile &profile) {
  std::vector<std::string> ids;
  ids.reserve(profile.enabled.size());
  for (const auto &kv : profile.enabled) {
    ids.push_back(kv.first);
  }
  std::sort(ids.begin(), ids.end());

  unsigned with = 0;
  for (const std::string &id : ids) {
    if (hasChecker(id.c_str())) {
      ++with;
    }
  }

  llvm::outs() << "profile: " << profile.name << "\n";
  llvm::outs() << "languages:";
  for (const std::string &lang : profile.languages) {
    llvm::outs() << " " << lang;
  }
  llvm::outs() << "\n";
  llvm::outs() << "enabled: " << ids.size() << "\n";
  llvm::outs() << "with-checker: " << with << "\n";
  llvm::outs() << "no-checker: " << (ids.size() - with) << "\n";
  for (const std::string &id : ids) {
    const char *severity = profile.severityOf(id.c_str());
    const char *kind = profile.kindOf(id.c_str());
    llvm::outs() << id << "\t" << (severity ? severity : "") << "\t" << (kind ? kind : "") << "\t"
                 << (hasChecker(id.c_str()) ? "checker" : "no-checker") << "\n";
  }
  return 0;
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

  if (opt.list) {
    return printRuleList(profile);
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
    const std::string resourceDir = clangResourceDir();
    tool.appendArgumentsAdjuster(
        [tgt, extras, resourceDir](const CommandLineArguments &args, llvm::StringRef) {
          CommandLineArguments out;
          const bool gnuArm =
              !args.empty() &&
              (llvm::StringRef(args.front()).contains_insensitive("arm-none-eabi-gcc") ||
               llvm::StringRef(args.front()).contains_insensitive("arm-none-eabi-g++"));
          std::string gccinc;
          if (!args.empty()) {
            out.push_back(args.front());
            if (gnuArm) {
              out.push_back(tgt);
              const std::string sys = gnuArmSysroot(args.front());
              if (!sys.empty()) {
                out.push_back("--sysroot=" + sys);
              }
              gccinc = gnuArmGccInclude(args.front());
            }
          }
          bool sawGccInclude = false;
          for (size_t i = 1; i < args.size(); ++i) {
            const llvm::StringRef arg(args[i]);
            if (gnuArm && dropGccArg(arg)) {
              continue;
            }
            llvm::StringRef sysPath;
            if (gnuArm && arg == "-isystem" && i + 1 < args.size()) {
              sysPath = args[i + 1];
            } else if (gnuArm && arg.starts_with("-isystem") &&
                       arg.size() > llvm::StringRef("-isystem").size()) {
              sysPath = arg.substr(llvm::StringRef("-isystem").size());
            }
            if (!sysPath.empty() && isGccBuiltinInclude(sysPath)) {
              // -nostdinc suppresses the implicit resource include. An explicit
              // -isystem keeps <arm_acle.h> on Clang's header, ahead of GCC's.
              if (!sawGccInclude && !resourceDir.empty()) {
                llvm::SmallString<256> inc(resourceDir);
                llvm::sys::path::append(inc, "include");
                out.push_back("-isystem");
                out.push_back(std::string(inc));
              }
              out.push_back("-idirafter");
              out.push_back(sysPath.str());
              sawGccInclude = true;
              if (arg == "-isystem") {
                ++i;
              }
              continue;
            }
            out.push_back(args[i]);
          }
          if (gnuArm && !sawGccInclude && !gccinc.empty()) {
            out.push_back("-idirafter");
            out.push_back(std::move(gccinc));
          }
          if (gnuArm && !args.empty()) {
            appendGnuArmCxxIncludes(out, args.front(), args);
          }
          if (!resourceDir.empty()) {
            out.push_back("-resource-dir");
            out.push_back(resourceDir);
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
  llvm::outs() << "no-checker: " << countMissingCheckers(profile) << "\n";

  if (parse_rc != 0) {
    return parse_rc;
  }
  return reporter.count() ? 1 : 0;
}
