#pragma once

#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace clang {
class SourceLocation;
class SourceManager;
} // namespace clang

class Profile;

// A diagnostic site that outlives the translation unit it was parsed from.
struct DiagSite {
  std::string file;
  unsigned line = 0;
  unsigned col = 0;
  // System headers, scratch space, and the predefined buffer are not reported.
  bool reportable = false;
};

class Reporter {
public:
  explicit Reporter(const Profile &profile);

  void emit(const clang::SourceManager &sm,
            clang::SourceLocation loc,
            const char *id,
            const char *msg);

  // Spelling location, with the same macro and system-header policy as emit.
  std::optional<DiagSite> locate(const clang::SourceManager &sm, clang::SourceLocation loc) const;
  void emitSite(const DiagSite &site, const char *id, const char *msg);

  unsigned count() const { return findings; }
  void printSummary() const;
  bool allows(const char *id, const char *name) const;

  // `--suppress <path>` or `--suppress <rule>:<path>`. A path matches the
  // diagnostic file in full, or as a suffix at a directory boundary.
  bool addSuppress(const std::string &text, std::string &err);
  // A saved siliscope report. A finding already listed at the same file,
  // line, column, severity, message, and rule is not printed again.
  bool loadBaseline(const std::string &path, std::string &err);

private:
  struct SeenDiag {
    std::string file;
    unsigned line = 0;
    unsigned col = 0;
    std::string id;
    std::string msg;
    bool operator<(const SeenDiag &other) const;
  };

  struct SuppressSpec {
    std::string rule; // empty: every rule in the file
    std::string path;
  };

  struct BaselineEntry {
    std::string file;
    unsigned line = 0;
    unsigned col = 0;
    std::string severity;
    std::string msg;
    std::string id;
    bool used = false;
  };

  bool drop(const SeenDiag &key, const char *severity);
  bool claimBaseline(const SeenDiag &key, const char *severity);

  const Profile *profile;
  unsigned findings = 0;
  unsigned suppressed = 0;
  bool filter_active = false;
  bool baseline_active = false;
  std::vector<SuppressSpec> suppress;
  std::vector<BaselineEntry> baseline;
  // line, column, severity, message, and rule. The file is matched after.
  std::unordered_map<std::string, std::vector<unsigned>> baseline_at;
  // One run checks a header again in every translation unit.
  std::set<SeenDiag> seen;
};
