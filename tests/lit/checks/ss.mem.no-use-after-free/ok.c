static int live(void) {
  int x;
  int *p = 0;
  x = 1;
  p = &x;
  *p = 2;
  return x;
}

static int poison(void) {
  int x;
  int *p = 0;
  x = 1;
  p = &x;
  *p = 2;
  p = 0;
  return x;
}
