void *memcpy(void *dst, const void *src, unsigned long n);

struct Pair {
  int a;
  int b;
};

static void same_buf(void) {
  char buf[8];
  buf[0] = 1;
  (void)memcpy(buf, buf, 4U);
}

static void same_struct(void) {
  struct Pair s;
  s.a = 1;
  s.b = 2;
  s = s;
}
