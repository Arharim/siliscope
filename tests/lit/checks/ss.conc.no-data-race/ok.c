static volatile int flag;

/* queued to the uart task */
static int pending;

static int total;

void USART1_IRQHandler(void) {
  flag = 1;
  pending = 1;
}

static void reader(void) {
  if (flag != 0) {
    flag = 0;
  }
}

static void uart_task(void) {
  if (pending != 0) {
    pending = 0;
  }
}

static void add(int n) { total = total + n; }

static void show(void) { (void)total; }

static void sensor_task(void) { flag = 1; }

static void report_task(void) {
  if (flag != 0) {
    flag = 0;
  }
}

int main(void) {
  reader();
  uart_task();
  add(1);
  show();
  sensor_task();
  report_task();
  return 0;
}
