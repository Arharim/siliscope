/* ss-run: also=other_ok.c expect=clean */
#include "api.h"

static volatile int flag;

void systick_isr(void) {
  flag = 1;
  helper();
}

void thread(void) { worker(); }
