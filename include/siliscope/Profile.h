#pragma once

#include <string>
#include <unordered_map>

class Profile {
public:
  std::string name;
  // Enabled rule id -> severity (error|warning|advisory|style).
  std::unordered_map<std::string, std::string> enabled;

  bool isEnabled(const char *id) const { return enabled.find(std::string(id)) != enabled.end(); }

  const char *severityOf(const char *id) const {
    const auto it = enabled.find(std::string(id));
    return it == enabled.end() ? nullptr : it->second.c_str();
  }
};

bool loadProfile(const std::string &ruleset_dir,
                 const std::string &name,
                 Profile &out,
                 std::string &err);
