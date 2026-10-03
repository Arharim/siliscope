/* ss-run: profile=embedded-cpp expect=ss.cpp.init-members */
struct Sample {
  int value;
  int other;
  Sample() : other(1) {}
};

struct Reordered {
  int value;
  int other;
  Reordered() : other(1), value(0) {}
};

struct Assigned {
  int value;
  Assigned() { value = 1; }
};

struct Defaulted {
  int value;
  Defaulted() = default;
};

struct Pod {
  int n;
};

struct Bare : Pod {
  Bare() {}
};
