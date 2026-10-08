void free(void *p);

static void sink(void *q);

static int star(void) {
  int x;
  int *p;
  x = 1;
  p = &x;
  free(p);
  return *p;
}

static void pass(void) {
  int x;
  int *p;
  x = 1;
  p = &x;
  free(p);
  sink(p);
}
