/* ss-run: profile=embedded-cpp expect=ss.conv.no-object-ptr-cast */
struct A {
  int n;
};

struct B {
  int n;
};

static const B *bad(const A *p) {
  return reinterpret_cast<const B *>(p);
}
