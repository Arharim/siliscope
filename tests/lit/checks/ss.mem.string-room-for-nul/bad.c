char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, unsigned long n);

static void lit(void) {
  char dst[4];
  dst[0] = 0;
  (void)strcpy(dst, "abcd");
}

static void ncopy(void) {
  char dst[4];
  dst[0] = 0;
  (void)strncpy(dst, "ab", 4U);
}
