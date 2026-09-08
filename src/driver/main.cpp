#include "siliscope/Frontend.h"
#include "siliscope/Version.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

void print_usage(std::FILE *out) {
  std::fputs("siliscope " SILISCOPE_VERSION_STRING "\n"
             "Embedded C/C++ analyzer (Clang LibTooling).\n"
             "\n"
             "Usage:\n"
             "  siliscope [options] <source>...\n"
             "\n"
             "Options:\n"
             "  --profile <name>       embedded-c | embedded-cpp | strict | style\n"
             "  --ruleset-dir <path>   catalog directory (default: ./ruleset)\n"
             "  -p <dir>               compilation database directory\n"
             "  --target <triple>      e.g. arm-none-eabi (also injected into -p jobs)\n"
             "  -extra-arg <arg>       extra Clang frontend argument (repeatable)\n"
             "  --probe                print AST probe (functions, interrupt, packed)\n"
             "  --version              print version and frontend status\n"
             "  -h, --help             this help\n",
             out);
}

} // namespace

int main(int argc, char **argv) {
  bool want_help = false;
  bool want_version = false;
  FrontendOptions fe;

  for (int i = 1; i < argc; ++i) {
    const char *a = argv[i];
    if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
      want_help = true;
    } else if (std::strcmp(a, "--version") == 0) {
      want_version = true;
    } else if (std::strcmp(a, "--profile") == 0 && i + 1 < argc) {
      fe.profile = argv[++i];
    } else if (std::strcmp(a, "--ruleset-dir") == 0 && i + 1 < argc) {
      fe.ruleset_dir = argv[++i];
    } else if (std::strcmp(a, "-p") == 0 && i + 1 < argc) {
      fe.compile_commands_dir = argv[++i];
    } else if (std::strcmp(a, "--target") == 0 && i + 1 < argc) {
      fe.target = argv[++i];
    } else if (std::strcmp(a, "-extra-arg") == 0 && i + 1 < argc) {
      fe.extra_args.emplace_back(argv[++i]);
    } else if (std::strcmp(a, "--probe") == 0) {
      fe.probe = true;
    } else if (a[0] == '-') {
      std::fprintf(stderr, "error: unknown option %s\n", a);
      return 2;
    } else {
      fe.sources.emplace_back(a);
    }
  }

  if (want_help) {
    print_usage(stdout);
    return 0;
  }

  if (want_version) {
    std::printf("siliscope %s\n", SILISCOPE_VERSION_STRING);
#ifdef SILISCOPE_WITH_CLANG
    std::puts("frontend: Clang LibTooling (enabled)");
#else
    std::puts("frontend: stub (rebuild with -DSILISCOPE_ENABLE_CLANG=ON)");
#endif
    std::printf("default profile: %s\n", fe.profile.c_str());
    std::printf("ruleset-dir: %s\n", fe.ruleset_dir.c_str());
    return 0;
  }

  if (fe.sources.empty()) {
    print_usage(stderr);
    std::fputs("\nerror: no source files\n", stderr);
    return 2;
  }

#ifdef SILISCOPE_WITH_CLANG
  return runFrontend(fe);
#else
  std::fputs("error: rebuild with -DSILISCOPE_ENABLE_CLANG=ON\n", stderr);
  return 2;
#endif
}
