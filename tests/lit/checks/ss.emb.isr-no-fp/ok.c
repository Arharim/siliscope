volatile int flag;

void USART1_IRQHandler(void) {
  flag = 1;
}

void systick_isr(void) {
  flag = flag + 1;
}

void thread(void) {
  volatile float x = 1.0F;
  x = x + 1.0F;
}
