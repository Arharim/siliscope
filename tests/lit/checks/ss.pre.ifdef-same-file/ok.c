/* ss-run: allow=ss.pre.no-unused-include:guard.h expect=clean */
#include "guard.h"

#if 0
#endif

#if 1
static int use(void) {
  return 1;
}
#endif
