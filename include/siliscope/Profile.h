#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct EnabledRule {
  std::string severity;
  std::string kind; // syntax, types, cfg, dataflow, callgraph, review, process
};

class Profile {
public:
  std::string name;
  // Languages this profile applies to (c, cpp). Empty means no language filter.
  std::vector<std::string> languages;
  // Enabled rule id -> severity and catalog check kind.
  std::unordered_map<std::string, EnabledRule> enabled;
  // Rule id -> allowed identifiers (e.g. log_printf for ss.fn.no-stdarg).
  std::unordered_map<std::string, std::vector<std::string>> allow;

  bool isEnabled(const char *id) const { return enabled.find(std::string(id)) != enabled.end(); }

  const char *severityOf(const char *id) const {
    const auto it = enabled.find(std::string(id));
    return it == enabled.end() ? nullptr : it->second.severity.c_str();
  }

  const char *kindOf(const char *id) const {
    const auto it = enabled.find(std::string(id));
    return it == enabled.end() ? nullptr : it->second.kind.c_str();
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
