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
  bool probe = false;
};

#ifdef SILISCOPE_WITH_CLANG
int runFrontend(const FrontendOptions &opt);
#endif
