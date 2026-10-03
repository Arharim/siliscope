/* ss-run: profile=embedded-cpp expect=clean */
int narrowed(int x) { return static_cast<int>(x); }
