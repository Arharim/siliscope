/* ss-run: profile=embedded-cpp expect=clean */
struct Sample {
  int n;
};

static const int *ok(const void *p) {
  return static_cast<const int *>(p);
}

static const char *as_bytes(const Sample *p) {
  return reinterpret_cast<const char *>(p);
}
