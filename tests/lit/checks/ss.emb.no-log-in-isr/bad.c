void log_printf(const char *msg);

void helper(void) {
  log_printf("x");
}

void USART1_IRQHandler(void) {
  log_printf("irq");
}

void systick_isr(void) {
  helper();
}
