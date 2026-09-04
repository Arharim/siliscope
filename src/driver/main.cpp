#include "siliscope/Version.h"

#include <cstdio>
#include <cstring>
#include <string>

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
             "  --target <triple>      e.g. arm-none-eabi (used without a database)\n"
             "  -extra-arg <arg>       extra Clang frontend argument (repeatable)\n"
             "  --version              print version and frontend status\n"
             "  -h, --help             this help\n",
             out);
}

} // namespace

int main(int argc, char **argv) {
  std::string profile = "embedded-c";
  std::string ruleset_dir = "ruleset";
  bool want_help = false;
  bool want_version = false;

  for (int i = 1; i < argc; ++i) {
    const char *a = argv[i];
    if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
      want_help = true;
    } else if (std::strcmp(a, "--version") == 0) {
      want_version = true;
    } else if (std::strcmp(a, "--profile") == 0 && i + 1 < argc) {
      profile = argv[++i];
    } else if (std::strcmp(a, "--ruleset-dir") == 0 && i + 1 < argc) {
      ruleset_dir = argv[++i];
    } else if (a[0] == '-') {
      // Flags implemented in later phases are accepted so scripts do not break.
      if ((std::strcmp(a, "-p") == 0 || std::strcmp(a, "--target") == 0 ||
           std::strcmp(a, "-extra-arg") == 0) &&
          i + 1 < argc) {
        ++i;
      }
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
    std::printf("default profile: %s\n", profile.c_str());
    std::printf("ruleset-dir: %s\n", ruleset_dir.c_str());
    return 0;
  }

  print_usage(stderr);
  std::fputs("\nerror: analysis is not wired yet (phase 0 skeleton)\n", stderr);
  return 2;
}
