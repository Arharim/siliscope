static int live(int x) {
  if (x) {
    return 1;
  }
  return 0;
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

static void skip(void) {
  while (0) {
  }
}

static int width(void) {
  int x;
  x = 0;
  if (sizeof(int) == 4U) {
    x = 1;
  }
  return x;
}

static int documented(void) {
  int x;
  x = 0;
  /* static_assert stand-in */
  if (1) {
    x = 1;
  }
  return x;
}
