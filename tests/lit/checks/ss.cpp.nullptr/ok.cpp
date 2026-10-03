/* ss-run: profile=embedded-cpp expect=clean */
int zero() { return 0; }

int from_ptr(const int *p) { return p == nullptr ? 1 : 0; }
