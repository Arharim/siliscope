/* ss-run: profile=embedded-cpp expect=clean */
struct Base {
  virtual int id() const { return 1; }
  virtual ~Base() = default;
};

struct Derived : Base {
  int id() const override { return 2; }
  ~Derived() override = default;
};

struct Closed final : Base {
  int id() const final { return 3; }
  ~Closed() override = default;
};

int kind(const Base *base) {
  if (base == nullptr) {
    return 0;
  }
  return base->id();
}
