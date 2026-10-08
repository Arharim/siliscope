/* ss-run: also=other.c expect=ss.conc.no-block-in-cs */
#include "block_api.h"

void taskENTER_CRITICAL(void);
void taskEXIT_CRITICAL(void);

void f(void) {
  taskENTER_CRITICAL();
  helper();
  taskEXIT_CRITICAL();
}
