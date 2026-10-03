/* ss-run: profile=embedded-cpp expect=ss.cpp.no-implicit-conversion */
struct Widget {
  Widget(int raw) : value_(raw) {}
  operator int() const { return value_; }

private:
  int value_;
};

int read(Widget widget) { return static_cast<int>(widget); }
