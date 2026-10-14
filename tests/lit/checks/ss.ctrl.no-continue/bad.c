/* ss-run: expect=clean */
/* ss-run: profile=strict expect=ss.ctrl.no-continue */
static void f(int x) {
  while (x != 0) {
    if (x == 1) {
      continue;
    }
    --x;
  }
}
