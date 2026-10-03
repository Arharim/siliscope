/* ss-run: profile=embedded-cpp expect=clean */
struct Widget {
  explicit Widget(int raw) : value_(raw) {}
  explicit operator int() const { return value_; }
  Widget(int left, int right) : value_(left + right) {}
  Widget(const Widget &) = default;

private:
  int value_;
};

int read(const Widget *widget) {
  if (widget == nullptr) {
    return 0;
  }
  return static_cast<int>(*widget);
}
