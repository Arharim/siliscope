/* ss-run: profile=embedded-cpp expect=ss.cpp.special-members */
struct OnlyDtor {
  ~OnlyDtor() = default;
  int read() const { return value_; }

private:
  int value_ = 0;
};

struct OnlyCopy {
  OnlyCopy(const OnlyCopy &) = default;
  int read() const { return value_; }

private:
  int value_ = 0;
};

union Half {
  int a;
  unsigned b;
  ~Half() = default;
};

int read(const OnlyDtor *item) {
  if (item == nullptr) {
    return 0;
  }
  return item->read();
}
