/* ss-run: profile=embedded-cpp expect=ss.cpp.no-cstyle-cast */
int narrowed(int x) {
  return (int)(unsigned char)x;
}

int wrapped(int x) { return int(x); }
