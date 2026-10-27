/* ss-run: allow=ss.emb.localize-extensions:systick_isr allow=ss.conc.no-volatile-mutex:systick_isr expect=clean */
/* numeric */
static volatile int flag;

#if defined(__GNUC__) // GNU interrupt attribute
void systick_isr(void) __attribute__((interrupt));
#endif

void USART1_IRQHandler(void) {
  flag = 1;
}

void systick_isr(void) {
  flag = flag + 1;
}

static void thread(void) {
  volatile float x = 1.0F;
  x = x + 1.0F;
}

int main(void) {
  thread();
  return 0;
}
