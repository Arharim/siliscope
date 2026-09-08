#include "siliscope/Report.h"

#include "siliscope/Profile.h"

#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/raw_ostream.h"

Reporter::Reporter(const Profile &profile) : profile(&profile) {}

void Reporter::emit(const clang::SourceManager &sm,
                    clang::SourceLocation loc,
                    const char *id,
                    const char *msg) {
  const char *severity = profile->severityOf(id);
  if (!severity) {
    return;
  }
  if (loc.isInvalid()) {
    return;
  }
  loc = sm.getExpansionLoc(loc);
  if (sm.isInSystemHeader(loc)) {
    return;
  }
  const clang::PresumedLoc pl = sm.getPresumedLoc(loc);
  if (pl.isInvalid()) {
    return;
  }
  ++findings;
  llvm::outs() << pl.getFilename() << ":" << pl.getLine() << ":" << pl.getColumn() << ": "
               << severity << ": " << msg << " [" << id << "]\n";
}

void Reporter::printSummary() const {
  llvm::outs() << "findings: " << findings << "\n";
}
