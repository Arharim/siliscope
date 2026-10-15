static volatile int flag;

void USART1_IRQHandler(void) { flag = 1; }

static void thread(void) {
  if (flag != 0) {
    flag = 0;
  }
}

int main(void) {
  thread();
  return 0;
}
