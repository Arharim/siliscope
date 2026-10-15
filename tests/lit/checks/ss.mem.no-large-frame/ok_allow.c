/* ss-run: allow=ss.mem.no-large-frame:work expect=clean */
static int work(void) {
  enum { kBig = 600 };
  char buf[kBig];
  buf[0] = 0;
  return buf[0];
}
