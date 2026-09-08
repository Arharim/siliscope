void taskENTER_CRITICAL(void);
void taskEXIT_CRITICAL(void);
void __disable_irq(void);
unsigned __get_PRIMASK(void);
void __set_PRIMASK(unsigned m);

void balanced(void) {
  taskENTER_CRITICAL();
  taskEXIT_CRITICAL();
}

void both_returns(int x) {
  taskENTER_CRITICAL();
  if (x) {
    taskEXIT_CRITICAL();
    return;
  }
  taskEXIT_CRITICAL();
}

void irq_pair(void) {
  const unsigned m = __get_PRIMASK();
  __disable_irq();
  __set_PRIMASK(m);
}

void nested(void) {
  taskENTER_CRITICAL();
  taskENTER_CRITICAL();
  taskEXIT_CRITICAL();
  taskEXIT_CRITICAL();
}
