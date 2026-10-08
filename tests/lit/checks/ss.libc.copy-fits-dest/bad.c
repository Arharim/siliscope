void *memcpy(void *dst, const void *src, unsigned long n);

static void over(void) {
  char dst[4];
  char src[8];
  src[0] = 1;
  dst[0] = 0;
  (void)memcpy(dst, src, 8U);
}
