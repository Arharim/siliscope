static void call(const int *p) { (void)p; }

static int local(void) {
  int x;
  int *p = 0;
  x = 1;
  p = &x;
  *p = 2;
  return x;
}

static void passed(void) {
  int x;
  x = 1;
  call(&x);
}
