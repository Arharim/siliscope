/* ss-run: also=other_ok.c allow=ss.emb.localize-extensions:handler expect=clean */
#include "api.h"

void thread(void) { worker(); }
