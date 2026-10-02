static int read_bad(const int *cp, volatile int *vp) {
  int *a = (int *)cp;
  int *b = (int *)vp;
  a = (int *)cp;
  return *a + *b;
}
