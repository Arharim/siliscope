/* ss-run: profile=embedded-cpp expect=clean */
struct Inner {
  int value;
  Inner() : value(0) {}
  Inner(const Inner &) = default;
};

struct Pod {
  int n;
};

struct Sample : Pod {
  Inner inner;
  int extra = 2;
  int count;
  Sample() : Pod(), count(1) {}
};

struct Reset {
  int value = 0;
  Reset() = default;
};

union Word {
  int a;
  unsigned b;
  Word() : a(0) {}
};

int read(const Sample *sample) { return sample->count + sample->extra + sample->inner.value; }
