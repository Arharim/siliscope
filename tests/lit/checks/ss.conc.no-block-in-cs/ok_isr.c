void taskENTER_CRITICAL(void);
void taskEXIT_CRITICAL(void);
void xQueueReceiveFromISR(void *q, void *msg, void *woken);

static void f(void) {
  taskENTER_CRITICAL();
  xQueueReceiveFromISR(0, 0, 0);
  taskEXIT_CRITICAL();
}
