struct Sample {
  int n;
};

static const int *from_void(const void *p) {
  return (const int *)p;
}

static const char *as_bytes(const struct Sample *p) {
  return (const char *)p;
}
