static void semi(volatile int *flag) {
  while (*flag == 0)
    ;
}

static void braces(volatile int *flag) {
  while (*flag == 0) {
  }
}

static void forever(void) {
  for (;;) {
  }
}

static void dose(volatile int *flag) {
  do {
  } while (*flag == 0);
}

static void commented_semi(volatile int *flag) {
  while (*flag == 0)
    ; // wait for the flag
}

static void after(volatile int *flag) {
  while (*flag == 0) {
  }
  // this note is for the next statement
}

static void url(volatile int *flag) {
  while (*flag == 0 && "http://wait"[0] == 'h') {
  }
}

static void count(void) {
  for (int i = 0; i < 4; ++i) {
  }
}

#define SPIN(flag) while (*(flag) == 0) {}

static void via_spin(volatile int *flag) { SPIN(flag); }
