/* ss-run: expect=clean */
/* ss-run: extra=-ffreestanding expect=clean */

static int g;

static void helper(void) {
}

void USART1_IRQHandler(void) {
}

int main(void) {
  helper();
  return g;
}

void _init(void) {
}

void _fini(void) {
}
