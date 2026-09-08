void log_printf(const char *msg);

volatile int flag;

#if defined(__GNUC__)
void nvic_isr(void) __attribute__((interrupt));
#endif

void nvic_isr(void) {
  flag = 1;
}

void USART1_IRQHandler(void) {
  flag = 1;
}

void thread(void) {
  log_printf("boot");
}
