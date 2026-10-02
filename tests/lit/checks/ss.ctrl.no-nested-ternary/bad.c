/* ss-run: expect=clean */
/* ss-run: profile=strict expect=ss.ctrl.no-nested-ternary */
static void f(int x) {
  int y;
  y = x ? (x == 1 ? 1 : 2) : 0;
  (void)y;
}
