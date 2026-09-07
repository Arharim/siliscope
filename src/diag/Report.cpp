#include "siliscope/Report.h"

#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/raw_ostream.h"

void Reporter::emit(const clang::SourceManager &sm,
                    clang::SourceLocation loc,
                    const char *severity,
                    const char *id,
                    const char *msg) {
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
