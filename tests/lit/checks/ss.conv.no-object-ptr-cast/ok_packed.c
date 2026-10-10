struct __attribute__((packed)) Wire {
  unsigned char n;
};

struct Sample {
  int n;
};

static const struct Wire *overlay(const struct Sample *p) {
  return (const struct Wire *)p;
}
