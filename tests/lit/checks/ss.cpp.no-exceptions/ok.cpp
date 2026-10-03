/* ss-run: profile=embedded-cpp expect=clean */
int parse(int x) {
  if (x < 0) {
    return 0;
  }
  return x;
}
