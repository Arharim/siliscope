/* ss-run: allow=ss.pre.prefer-inline:WAIT_FLAG expect=clean */
static void poke(volatile int *flag) { *flag = 1; }

static void inside(volatile int *flag) {
  while (*flag == 0) {
    /* wait until the peripheral sets the flag */
  }
}

static void above(volatile int *flag) {
  /* The status bit stays clear until the shift finishes. */
  while (*flag == 0) {
  }
}

static void block(volatile int *flag) {
  /* Wait until the peripheral leaves busy.
     The flag is the hardware status bit. */
  while (*flag == 0) {
  }
}

static void trailing(volatile int *flag) {
  while (*flag == 0) {
  } // peripheral ready
}

static void same_line(volatile int *flag) {
  (void)flag;
  for (;;) { // idle until an interrupt wakes the core
  }
}

static void dose(volatile int *flag) {
  do {
    /* poll until the flag is set */
  } while (*flag == 0);
}

static void line_above(volatile int *flag) {
  // spin until the timeout bit is set
  while (*flag == 0) {
  }
}

static void poll(volatile int *flag) {
  while (*flag == 0) {
    poke(flag);
  }
}

#define WAIT_FLAG(flag) \
  while (*(flag) == 0) { \
    /* wait until the peripheral sets the flag */ \
  }

static void via_macro(volatile int *flag) { WAIT_FLAG(flag); }

static void noop(void) {
  do {
  } while (0);
  while (0) {
  }
}
