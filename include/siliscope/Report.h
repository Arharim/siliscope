#pragma once

#include <set>
#include <string>

namespace clang {
class SourceLocation;
class SourceManager;
} // namespace clang

class Profile;

class Reporter {
public:
  explicit Reporter(const Profile &profile);

  void emit(const clang::SourceManager &sm,
            clang::SourceLocation loc,
            const char *id,
            const char *msg);

  unsigned count() const { return findings; }
  void printSummary() const;
  bool allows(const char *id, const char *name) const;

private:
  struct SeenDiag {
    std::string file;
    unsigned line = 0;
    unsigned col = 0;
    std::string id;
    std::string msg;
    bool operator<(const SeenDiag &other) const;
  };

  const Profile *profile;
  unsigned findings = 0;
  // One run checks a header again in every translation unit.
  std::set<SeenDiag> seen;
};
