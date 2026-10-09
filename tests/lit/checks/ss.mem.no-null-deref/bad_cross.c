/* ss-run: also=other.c expect=ss.mem.no-null-deref */
#include "call_api.h"

int use(void) {
  int *const p = 0;
  set(p);
  return 0;
}
