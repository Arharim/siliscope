#pragma once

#include <string>
#include <utility>
#include <vector>

struct FrontendOptions {
  std::string target = "arm-none-eabi";
  std::string profile = "embedded-c";
  std::string ruleset_dir = "ruleset";
  std::string compile_commands_dir;
  std::vector<std::string> extra_args;
  std::vector<std::string> sources;
  // --allow rule:name (e.g. ss.fn.no-stdarg:log_printf)
  std::vector<std::pair<std::string, std::string>> allow;
  // --suppress path, --suppress rule:path, or either with a trailing slash
  // for a directory prefix. Repeatable.
  std::vector<std::string> suppress;
  // Saved report. Findings listed in it are not printed again.
  std::string baseline;
  bool probe = false;
  // Print enabled rules and whether a checker is linked. Does not parse sources.
  bool list = false;
};

#ifdef SILISCOPE_WITH_CLANG
int runFrontend(const FrontendOptions &opt);
#endif
