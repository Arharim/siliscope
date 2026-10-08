void *memcpy(void *dst, const void *src, unsigned long n);
void *memset(void *dst, int value, unsigned long n);

static void fits(void) {
  char dst[8];
  char src[4];
  src[0] = 1;
  (void)memset(dst, 0, 8U);
  (void)memcpy(dst, src, 4U);
}
