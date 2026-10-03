/* ss-run: profile=embedded-cpp expect=ss.cpp.no-exceptions */
int parse(int x) {
  try {
    if (x < 0) {
      throw x;
    }
  } catch (int) {
    return 0;
  }
  return x;
}
