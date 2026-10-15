void xQueueSendFromISR(void *queue, const void *item, int *woken);

static volatile int flag;

static void *queue = 0;

void USART1_IRQHandler(void) {
  int woken = 0;
  flag = 1;
  xQueueSendFromISR(queue, 0, &woken);
}

int main(void) {
  (void)flag;
  (void)queue;
  return 0;
}
