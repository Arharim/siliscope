/* ss-run: profile=embedded-cpp expect=clean */
struct Base {
  virtual int id() const { return 1; }
  virtual ~Base() = default;
};

struct Derived : Base {
  int id() const override { return 2; }
};

struct ProtectedBase {
  virtual int id() const { return 3; }

protected:
  ~ProtectedBase() = default;
};

struct Child : ProtectedBase {
  int id() const override { return 4; }
  virtual ~Child() = default;
};

struct Deleted {
  virtual int id() const { return 5; }
  ~Deleted() = delete;
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
