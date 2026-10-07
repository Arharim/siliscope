/* ss-run: allow=ss.pre.no-unused-include:guard.h expect=clean */
#include "guard.h"

// closed in this file
#if 0
#endif

// the body stays in this file
#if 1
static int use(void) {
  return 1;
}
#endif
