/* ss-run: allow=ss.pre.no-unused-include:./rel.h expect=clean */
#include "rel.h"
#include "./rel.h"

static int use(void) {
  return REL;
}
