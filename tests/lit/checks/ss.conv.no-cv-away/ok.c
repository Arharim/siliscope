static int read_ok(const int *p, volatile int *v) {
  const int *const q = p;
  volatile int *const w = v;
  return *q + *w;
}
