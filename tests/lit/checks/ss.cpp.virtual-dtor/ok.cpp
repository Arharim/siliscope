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
};

struct ProtectedBase {
  virtual int id() const { return 3; }
  ProtectedBase(const ProtectedBase &) = default;
  ProtectedBase &operator=(const ProtectedBase &) = default;
  ProtectedBase(ProtectedBase &&) = default;
  ProtectedBase &operator=(ProtectedBase &&) = default;

protected:
  ~ProtectedBase() = default;
};

struct Child : ProtectedBase {
  int id() const override { return 4; }
  virtual ~Child() = default;
  Child(const Child &) = default;
  Child &operator=(const Child &) = default;
  Child(Child &&) = default;
  Child &operator=(Child &&) = default;
};

struct Deleted {
  virtual int id() const { return 5; }
  ~Deleted() = delete;
  Deleted(const Deleted &) = delete;
  Deleted &operator=(const Deleted &) = delete;
  Deleted(Deleted &&) = delete;
  Deleted &operator=(Deleted &&) = delete;
};

struct Leaf final {
  virtual int id() const { return 6; }
};

int kind(const Base *base) {
  if (base == nullptr) {
    return 0;
  }
  return base->id();
}
