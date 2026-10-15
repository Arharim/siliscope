static int edge(void) {
  enum { kEdge = 512, kBig = 600 };
  char buf[kEdge];
  static char big[kBig];
  buf[0] = 0;
  big[0] = 1;
  return buf[0] != big[0];
}
