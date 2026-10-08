/* ss-run: also=other_ok.c expect=clean */
#include "api.h"

void thread(void) { worker(); }
