/* ss-run: profile=embedded-cpp expect=ss.cpp.override */
struct Base {
  virtual int id() const { return 1; }
  virtual ~Base() = default;
};

struct Derived : Base {
  int id() const { return 2; }
  ~Derived() override = default;
};

int kind(const Base *base) {
  if (base == nullptr) {
    return 0;
  }
  return base->id();
}
