/* ss-run: also=other_ok.c allow=ss.emb.localize-extensions:systick_isr expect=clean */
#include "api.h"

static volatile int flag;

#if defined(__GNUC__) // GNU interrupt attribute
void systick_isr(void) __attribute__((interrupt));
#endif

void systick_isr(void) {
  flag = 1;
  helper();
}

void thread(void) {
  flag = 0;
  worker();
}
