static int flag;

void USART1_IRQHandler(void) { flag = 1; }

static void reader(void) {
  if (flag != 0) {
    flag = 0;
  }
}

static int shared;

static void sensor_task(void) { shared = 1; }

static void report_task(void) {
  if (shared != 0) {
    shared = 0;
  }
}

int main(void) {
  reader();
  sensor_task();
  report_task();
  return 0;
}
