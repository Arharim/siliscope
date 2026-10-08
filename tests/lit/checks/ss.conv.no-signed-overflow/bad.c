static int add(void) {
  int a;
  a = 2000000000;
  return a + 2000000000;
}

static int neg(void) {
  int a;
  a = -2147483647 - 1;
  return -a;
}

static int shift(void) {
  int a;
  a = 1 << 31;
  return a;
}
