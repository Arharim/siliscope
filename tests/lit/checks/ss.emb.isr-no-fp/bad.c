void USART1_IRQHandler(void) {
  volatile float x = 1.0F;
  x = x + 1.0F;
}

void systick_isr(void) {
  volatile double d = 0.0;
  d = d + 1.0;
}
