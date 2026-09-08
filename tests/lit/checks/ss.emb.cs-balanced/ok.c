void taskENTER_CRITICAL(void);
void taskEXIT_CRITICAL(void);
void __disable_irq(void);
void __enable_irq(void);

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
  __disable_irq();
  __enable_irq();
}

void nested(void) {
  taskENTER_CRITICAL();
  taskENTER_CRITICAL();
  taskEXIT_CRITICAL();
  taskEXIT_CRITICAL();
}
