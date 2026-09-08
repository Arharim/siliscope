#pragma once

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

private:
  const Profile *profile;
  unsigned findings = 0;
};
