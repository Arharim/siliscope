void nvic_isr(void) {}

static void plain(void) {}

static void (*const vectors[1])(void) = {plain};

int main(void) {
  (void)vectors[0];
  return 0;
}
