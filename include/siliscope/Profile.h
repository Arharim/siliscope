#pragma once

#include <string>
#include <unordered_map>
#include <vector>

class Profile {
public:
  std::string name;
  // Enabled rule id -> severity (error|warning|advisory|style).
  std::unordered_map<std::string, std::string> enabled;
  // Rule id -> allowed identifiers (e.g. log_printf for ss.fn.no-stdarg).
  std::unordered_map<std::string, std::vector<std::string>> allow;

  bool isEnabled(const char *id) const { return enabled.find(std::string(id)) != enabled.end(); }

  const char *severityOf(const char *id) const {
    const auto it = enabled.find(std::string(id));
    return it == enabled.end() ? nullptr : it->second.c_str();
  }

  void addAllow(const std::string &rule, const std::string &name) { allow[rule].push_back(name); }

  bool allows(const char *rule, const char *name) const {
    if (!rule || !name) {
      return false;
    }
    const auto it = allow.find(rule);
    if (it == allow.end()) {
      return false;
    }
    for (const std::string &n : it->second) {
      if (n == name) {
        return true;
      }
    }
    return false;
  }
};

bool loadProfile(const std::string &ruleset_dir,
                 const std::string &name,
                 Profile &out,
                 std::string &err);
