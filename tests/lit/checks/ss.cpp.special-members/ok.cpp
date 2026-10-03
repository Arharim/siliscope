/* ss-run: profile=embedded-cpp expect=clean */
struct Zero {
  int value;
};

struct Reset {
  Reset() : value_(0) {}
  int read() const { return value_; }

private:
  int value_;
};

struct Complete {
  ~Complete() = default;
  Complete(const Complete &) = default;
  Complete &operator=(const Complete &) = default;
  Complete(Complete &&) = default;
  Complete &operator=(Complete &&) = default;
  int read() const { return value_; }

private:
  int value_ = 1;
};

struct Gone {
  ~Gone() = delete;
  Gone(const Gone &) = delete;
  Gone &operator=(const Gone &) = delete;
  Gone(Gone &&) = delete;
  Gone &operator=(Gone &&) = delete;
};

int read(const Reset *item) {
  if (item == nullptr) {
    return 0;
  }
  return item->read();
}
