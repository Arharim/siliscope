/* ss-run: allow=ss.emb.localize-extensions:Wire expect=clean */
struct __attribute__((packed)) Wire {
  unsigned char n;
  unsigned char kind;
};

_Static_assert(sizeof(struct Wire) == 2U, "size");
_Static_assert(__builtin_offsetof(struct Wire, n) == 0U, "n");
_Static_assert(__builtin_offsetof(struct Wire, kind) == 1U, "kind");

struct Sample {
  unsigned value;
};

static int use(unsigned char pick) {
  const struct Wire wire = {1, 1};
  const struct Sample sample = {1U};
  (void)sample;
  return pick != 0 ? wire.n : wire.kind;
}

int main(void) { return use(1); }
