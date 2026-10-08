char *strncpy(char *dst, const char *src, unsigned long n);

static void fits(void) {
  char dst[8];
  dst[0] = 0;
  (void)strncpy(dst, "ab", 4U);
}
