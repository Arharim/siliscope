/* ss-run: profile=embedded-cpp expect=clean */
struct Base {
  virtual int id() const { return 1; }
};

struct Derived : Base {
  int id() const override { return 2; }
};

int kind(const Base *b) {
  if (b == nullptr) {
    return 0;
  }
  return b->id();
}
