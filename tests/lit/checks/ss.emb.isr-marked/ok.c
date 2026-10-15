/* ss-run: allow=ss.emb.localize-extensions:nvic_isr expect=clean */
#if defined(__GNUC__) // GNU interrupt attribute
void nvic_isr(void) __attribute__((interrupt));
#endif

void nvic_isr(void);

void USART1_IRQHandler(void);

static void SysTick_Handler(void);

enum { kVectors = 3 };
static void (*const vectors[kVectors])(void) = {nvic_isr, USART1_IRQHandler, SysTick_Handler};

void nvic_isr(void) { (void)vectors[0]; }

void USART1_IRQHandler(void) { (void)vectors[1]; }

static void SysTick_Handler(void) { (void)vectors[2]; }

static void ringbuf_push_isr(int byte) { (void)byte; }

static void thread(void) { ringbuf_push_isr(1); }

int main(void) {
  thread();
  return 0;
}
