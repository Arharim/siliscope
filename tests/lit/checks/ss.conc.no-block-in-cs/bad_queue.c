void taskENTER_CRITICAL(void);
void taskEXIT_CRITICAL(void);
void xQueueReceive(void *q, void *msg, unsigned ticks);

static void f(void) {
  taskENTER_CRITICAL();
  xQueueReceive(0, 0, 1U);
  taskEXIT_CRITICAL();
}
