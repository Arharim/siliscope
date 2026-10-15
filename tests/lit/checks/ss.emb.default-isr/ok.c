enum { kVectors = 2 };

void USART1_IRQHandler(void);
void SysTick_IRQHandler(void);

static void (*const vectors[kVectors])(void) = {USART1_IRQHandler, SysTick_IRQHandler};

void USART1_IRQHandler(void) { (void)vectors[0]; }

void SysTick_IRQHandler(void) { (void)vectors[1]; }

int main(void) {
  (void)vectors[0];
  return 0;
}
