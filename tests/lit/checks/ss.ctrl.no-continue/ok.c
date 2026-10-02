/* ss-run: expect=clean */
/* ss-run: profile=strict expect=clean */
static void f(int x) {
  while (x) {
    --x;
  }
}
