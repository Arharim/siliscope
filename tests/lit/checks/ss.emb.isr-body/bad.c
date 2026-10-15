void vTaskDelay(int ticks);

void USART1_IRQHandler(void) { vTaskDelay(1); }

void SysTick_IRQHandler(void) {
  for (;;) {
    /* an interrupt does not spin */
  }
}

int main(void) { return 0; }
