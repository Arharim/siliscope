/* ss-run: profile=embedded-cpp expect=ss.cpp.no-friend */
struct Box {
  explicit Box(int raw) : value_(raw) {}
  friend int read(const Box *box) { return box->value_; }

private:
  int value_;
};
