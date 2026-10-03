/* ss-run: profile=embedded-cpp expect=clean */
struct Pod {
  int value;
  friend int read(const Pod *pod) { return pod->value; }
};
