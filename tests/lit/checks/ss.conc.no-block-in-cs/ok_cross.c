/* ss-run: also=other_ok.c expect=clean */
#include "block_api.h"

void taskENTER_CRITICAL(void);
void taskEXIT_CRITICAL(void);

void f(void) {
  taskENTER_CRITICAL();
  helper();
  taskEXIT_CRITICAL();
}
