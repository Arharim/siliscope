/* ss-run: also=recur_b.c expect=ss.ctrl.no-recursion */
#include "recur_api.h"

void a(void) { b(); }
