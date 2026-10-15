static volatile int flag;

static unsigned mmio(void) {
  volatile unsigned *const reg = (volatile unsigned *)0xE000EDFCu;
  *reg |= 1U;
  return *reg;
}

void USART1_IRQHandler(void) { flag = 1; }

int main(void) { return (int)mmio() + flag; }
