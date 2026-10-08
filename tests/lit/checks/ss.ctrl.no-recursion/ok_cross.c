/* ss-run: also=other.c expect=clean */
#include "ok_api.h"

static void rec(void) { other(); }

void start(void) {
  worker();
  rec();
}
