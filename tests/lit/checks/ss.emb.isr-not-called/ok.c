#if defined(__GNUC__)
void nvic_isr(void) __attribute__((interrupt));
#endif

void nvic_isr(void) {
}

void USART1_IRQHandler(void) {
}

void work(void) {
}

/* Vector-table entries take the address; that is not a call. */
void (*const vectors[2])(void) = {nvic_isr, USART1_IRQHandler};

void thread(void) {
  work();
}
