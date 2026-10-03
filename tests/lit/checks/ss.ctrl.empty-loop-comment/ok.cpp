static void flag(volatile int *ready) {
  while (*ready == 0) {
    /* wait until the peripheral sets the flag */
  }
}

static void noop(void) {
  do {
  } while (false);
}
