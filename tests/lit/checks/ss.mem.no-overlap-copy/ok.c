void *memcpy(void *dst, const void *src, unsigned long n);
void *memmove(void *dst, const void *src, unsigned long n);

struct Pair {
  int a;
  int b;
};

static void copies(void) {
  char a[8];
  char b[8];
  struct Pair s;
  struct Pair t;
  a[0] = 1;
  b[0] = 2;
  (void)memcpy(a, b, 4U);
  (void)memmove(a, a, 4U);
  s.a = 1;
  s.b = 2;
  t.a = 3;
  t.b = 4;
  s = t;
}
