static int past(void) {
  int a[4];
  int *p = a + 4;
  a[0] = 1;
  return *p;
}
