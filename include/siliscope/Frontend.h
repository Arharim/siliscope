#pragma once

#include <string>
#include <vector>

struct FrontendOptions {
  std::string target = "arm-none-eabi";
  std::string profile = "embedded-c";
  std::string ruleset_dir = "ruleset";
  std::string compile_commands_dir;
  std::vector<std::string> extra_args;
  std::vector<std::string> sources;
  bool probe = false;
};

#ifdef SILISCOPE_WITH_CLANG
int runFrontend(const FrontendOptions &opt);
#endif
