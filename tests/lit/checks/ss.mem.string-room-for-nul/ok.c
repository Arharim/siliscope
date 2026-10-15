char *strncpy(char *dst, const char *src, unsigned n);

static void fits(void) {
  enum { kDst = 8 };
  char dst[kDst];
  dst[0] = 0;
  (void)strncpy(dst, "ab", 4U);
}

int main(void) {
  fits();
  return 0;
}
