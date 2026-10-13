static int edge(void) {
  char buf[512];
  static char big[600];
  buf[0] = 0;
  big[0] = 1;
  return buf[0] + big[0];
}
