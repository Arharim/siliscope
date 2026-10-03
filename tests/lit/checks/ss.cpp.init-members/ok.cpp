/* ss-run: profile=embedded-cpp expect=clean */
struct Inner {
  Inner() : value_(0) {}
  Inner(const Inner &) = default;
  ~Inner() = default;
  Inner &operator=(const Inner &) = default;
  Inner(Inner &&) = default;
  Inner &operator=(Inner &&) = default;
  int value() const { return value_; }

private:
  int value_;
};

struct Pod {
  int n;
};

struct Sample : Pod {
  Sample() : Pod(), count_(1) {}
  int read() const { return count_ + extra_ + inner_.value(); }

private:
  Inner inner_;
  int extra_ = 2;
  int count_;
};

struct Reset {
  Reset() = default;

private:
  int value_ = 0;
};

union Word {
  int a;
  unsigned b;
  Word() : a(0) {}
};

int read(const Sample *sample) { return sample->read(); }
