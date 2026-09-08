void systick_isr(void) {
}

void USART1_IRQHandler(void) {
}

void thread(void) {
  systick_isr();
  USART1_IRQHandler();
}
