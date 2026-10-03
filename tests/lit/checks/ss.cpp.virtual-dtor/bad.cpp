/* ss-run: profile=embedded-cpp expect=ss.cpp.virtual-dtor */
struct Base {
  virtual int id() const { return 1; }
};

struct Named {
  virtual int id() const { return 2; }
  ~Named() = default;
};

struct Abstract {
  virtual int id() const = 0;
};

class PrivateDtor {
  virtual int id() const { return 3; }
  ~PrivateDtor() = default;
};

int kind(const Base *base) {
  if (base == nullptr) {
    return 0;
  }
  return base->id();
}
