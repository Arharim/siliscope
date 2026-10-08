void *malloc(unsigned int n);
void taskENTER_CRITICAL(void);
void taskEXIT_CRITICAL(void);

static void f(void) {
  taskENTER_CRITICAL();
  (void)malloc(4U);
  taskEXIT_CRITICAL();
}
