static int g;

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
  }
}

static void ptr_write(void) {
  int *q = &g;
  *q = 0;
  q = &g;
}
