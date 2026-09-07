#pragma once

namespace clang {
class SourceLocation;
class SourceManager;
} // namespace clang

class Reporter {
public:
  void emit(const clang::SourceManager &sm,
            clang::SourceLocation loc,
            const char *severity,
            const char *id,
            const char *msg);

  unsigned count() const { return findings; }
  void printSummary() const;

private:
  unsigned findings = 0;
};
