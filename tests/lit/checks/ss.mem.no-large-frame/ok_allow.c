/* ss-run: allow=ss.mem.no-large-frame:work expect=clean */
static int work(void) {
  char buf[600];
  buf[0] = 0;
  return buf[0];
}
