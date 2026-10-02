/* ss-run: expect=clean */
/* ss-run: profile=strict expect=clean */
static void f(int x) {
  int y;
  y = x ? 1 : 0;
  (void)y;
}
