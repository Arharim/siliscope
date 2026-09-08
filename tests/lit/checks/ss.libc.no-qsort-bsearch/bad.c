void qsort(void *, unsigned, unsigned, int (*)(const void *, const void *));
void *bsearch(const void *, const void *, unsigned, unsigned, int (*)(const void *, const void *));

int cmp(const void *a, const void *b) {
  (void)a;
  (void)b;
  return 0;
}

void f(void) {
  int x[2];
  x[0] = 0;
  x[1] = 1;
  qsort(x, 2, sizeof(int), cmp);
  (void)bsearch(x, x, 2, sizeof(int), cmp);
}
