/* ss-run: profile=embedded-cpp expect=ss.cpp.private-data */
struct Open {
  explicit Open(int raw) : value(raw) {}
  int value;
  static int shared;
};

struct Guarded {
  explicit Guarded(int raw) : value(raw) {}

protected:
  int value;
};
