static void counted(int n) {
  int i;
  int s;
  s = 0;
  for (i = 0; i < n; ++i) {
    s = s + i;
  }
  (void)s;
}

static void down(int n) {
  while (n) {
    --n;
  }
}

static void timeout(volatile int *flag) {
  for (int spins = 8; *flag == 0 && spins > 0; --spins) {
    /* peripheral */
  }
}

static void idle(void) {
  for (;;) {
    /* does not return */
  }
}

static void once(void) {
  do {
  } while (0);
}
