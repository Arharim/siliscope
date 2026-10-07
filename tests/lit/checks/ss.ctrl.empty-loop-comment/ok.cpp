static void flag(volatile int *ready) {
  for (int spins = 8; *ready == 0 && spins > 0; --spins) {
    /* wait until the peripheral sets the flag */
  }
}

static void noop(void) {
  do {
  } while (false);
}
