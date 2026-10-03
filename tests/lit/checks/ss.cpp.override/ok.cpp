/* ss-run: profile=embedded-cpp expect=clean */
struct Base {
  virtual int id() const { return 1; }
  virtual ~Base() = default;
  Base(const Base &) = default;
  Base &operator=(const Base &) = default;
  Base(Base &&) = default;
  Base &operator=(Base &&) = default;
};

struct Derived : Base {
  int id() const override { return 2; }
  ~Derived() override = default;
  Derived(const Derived &) = default;
  Derived &operator=(const Derived &) = default;
  Derived(Derived &&) = default;
  Derived &operator=(Derived &&) = default;
};

struct Closed final : Base {
  int id() const final { return 3; }
  ~Closed() override = default;
  Closed(const Closed &) = default;
  Closed &operator=(const Closed &) = default;
  Closed(Closed &&) = default;
  Closed &operator=(Closed &&) = default;
};

int kind(const Base *base) {
  if (base == nullptr) {
    return 0;
  }
  return base->id();
}
