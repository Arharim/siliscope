/* ss-run: profile=embedded-cpp expect=clean */
struct Widget {
  explicit Widget(int raw) : value_(raw) {}
  Widget(const Widget &other) : value_(other.value_) {}
  Widget(Widget &&other) noexcept : value_(other.value_) { other.value_ = 0; }
  ~Widget() = default;
  Widget &operator=(const Widget &other) noexcept {
    Widget copy(other);
    copy.value_ = other.value_;
    swap(copy);
    return *this;
  }
  Widget &operator=(Widget &&other) noexcept {
    swap(other);
    return *this;
  }
  void swap(Widget &other) noexcept {
    const int saved = value_;
    value_ = other.value_;
    other.value_ = saved;
  }
  int read() const { return value_; }

private:
  int value_;
};

struct Loose {
  explicit Loose(int raw) : value_(raw) {}
  Loose(const Loose &other) : value_(other.value_) {}
  Loose(Loose &&other) noexcept : value_(other.value_) { other.value_ = 0; }
  ~Loose() = default;
  Loose &operator=(const Loose &) = default;
  Loose &operator=(Loose &&) = default;
  void swap(Loose &other) {
    const int saved = value_;
    value_ = other.value_;
    other.value_ = saved;
  }
  int read() const { return value_; }

private:
  int value_;
};

void exchange(Loose &left, Loose &right) {
  left.swap(right);
  right.swap(left);
}

int read(const Widget *item) {
  if (item == nullptr) {
    return 0;
  }
  return item->read();
}
