static void writes(int *p) {
  *p = 1;
}

static void uses_const(const int *p) {
  int x = *p;
  x = x + 1;
  (void)x;
}

static void loop(int n) {
  int i;
  for (i = 0; i < n; ++i) {
    /* i is updated by the increment */
  }
}

static void ptr_write(void) {
  int g;
  int *q = &g;
  g = 0;
  *q = 0;
  q = &g;
}
