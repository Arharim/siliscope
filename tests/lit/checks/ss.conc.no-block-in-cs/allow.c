/* ss-run: allow=ss.conc.no-block-in-cs:vTaskDelay expect=clean */
void taskENTER_CRITICAL(void);
void taskEXIT_CRITICAL(void);
void vTaskDelay(unsigned ms);

static void f(void) {
  taskENTER_CRITICAL();
  vTaskDelay(1U);
  taskEXIT_CRITICAL();
}
