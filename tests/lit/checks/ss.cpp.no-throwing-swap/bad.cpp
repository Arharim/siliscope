/* ss-run: profile=embedded-cpp expect=ss.cpp.no-throwing-swap */
typedef unsigned int size_t;
void *operator new(size_t);
void operator delete(void *) noexcept;

struct Widget {
  explicit Widget(int raw) : value_(raw) {}
  Widget(const Widget &other) : value_(other.value_) {}
  Widget(Widget &&other) noexcept : value_(other.value_) { other.value_ = 0; }
  ~Widget() = default;
  Widget &operator=(const Widget &other) {
    Widget copy(other);
    copy.value_ = other.value_;
    swap(copy);
    return *this;
  }
  Widget &operator=(Widget &&other) {
    swap(other);
    return *this;
  }
  void swap(Widget &other) {
    int *const saved = new int(value_);
    value_ = other.value_;
    other.value_ = *saved;
    delete saved;
  }
  int read() const { return value_; }

private:
  int value_;
};

int read(const Widget *item) {
  if (item == nullptr) {
    return 0;
  }
  return item->read();
}
