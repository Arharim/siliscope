static volatile int drops;

void USART1_IRQHandler(void) { drops = drops + 1; }

int main(void) { return 0; }
