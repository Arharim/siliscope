/* ss-run: profile=embedded-cpp expect=clean */
struct Quiet {
  ~Quiet() = default;
};

struct Body {
  Body() : value_(0) {}
  ~Body() {}
  int read() const { return value_; }

private:
  int value_;
};

int read(const Body *body) { return body->read(); }
