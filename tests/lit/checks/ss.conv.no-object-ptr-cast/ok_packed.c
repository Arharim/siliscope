/* ss-run: allow=ss.emb.localize-extensions:Wire expect=clean */
struct __attribute__((packed)) Wire {
  unsigned char n;
};

_Static_assert(sizeof(struct Wire) == 1U, "size");
_Static_assert(__builtin_offsetof(struct Wire, n) == 0U, "n");

struct Sample {
  int n;
};

static const struct Wire *overlay(const struct Sample *p) {
  return (const struct Wire *)p;
}
