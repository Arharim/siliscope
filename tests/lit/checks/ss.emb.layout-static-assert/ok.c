/* ss-run: allow=ss.emb.localize-extensions:Wire expect=clean */
struct __attribute__((packed)) Wire {
  unsigned char n;
  unsigned char kind;
};

_Static_assert(sizeof(struct Wire) == 2U, "size");
_Static_assert(__builtin_offsetof(struct Wire, n) == 0U, "n");
_Static_assert(__builtin_offsetof(struct Wire, kind) == 1U, "kind");

static int use(const struct Wire *wire) { return wire->n != 0; }

int main(void) {
  enum { kKind = 2 };
  const struct Wire wire = {1, kKind};
  return use(&wire);
}
