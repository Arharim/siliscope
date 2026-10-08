void taskENTER_CRITICAL(void);
void taskEXIT_CRITICAL(void);
void vTaskDelay(unsigned ms);

static void work(void) {}

static void f(void) {
  taskENTER_CRITICAL();
  work();
  taskEXIT_CRITICAL();
  vTaskDelay(1U);
}
