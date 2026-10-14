#if defined(__GNUC__) // GNU interrupt attribute
void nvic_isr(void) __attribute__((interrupt));
#endif

void nvic_isr(void);
void USART1_IRQHandler(void);

/* Vector-table entries take the address; that is not a call. */
static void (*const vectors[2])(void) = {nvic_isr, USART1_IRQHandler};

void nvic_isr(void) { (void)vectors[0]; }

void USART1_IRQHandler(void) { (void)vectors[1]; }

static void work(void) {}

static void ringbuf_push_isr(int n) { (void)n; }

static void thread(void) {
  work();
  ringbuf_push_isr(1);
}

int main(void) {
  thread();
  return 0;
}
