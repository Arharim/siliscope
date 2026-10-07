static void flag(volatile int *ready) {
  while (*ready == 0) {
  }
}

static void leaves(void) {
  for (;;) {
    break;
  }
}
