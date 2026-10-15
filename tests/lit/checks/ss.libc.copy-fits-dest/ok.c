void *memcpy(void *dst, const void *src, unsigned n);
void *memset(void *dst, int value, unsigned n);

static void fits(void) {
  enum { kDst = 8, kSrc = 4 };
  char dst[kDst];
  char src[kSrc];
  src[0] = 1;
  (void)memset(dst, 0, 8U);
  (void)memcpy(dst, src, 4U);
}

int main(void) {
  fits();
  return 0;
}
