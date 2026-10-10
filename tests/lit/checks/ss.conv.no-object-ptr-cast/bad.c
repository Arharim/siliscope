struct A {
  int n;
};

struct B {
  int n;
};

static const struct B *bad(const struct A *p) {
  return (const struct B *)p;
}
