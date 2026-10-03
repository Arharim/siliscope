/* ss-run: profile=embedded-cpp expect=clean */
struct Quiet {
  ~Quiet() = default;
  Quiet(const Quiet &) = default;
  Quiet &operator=(const Quiet &) = default;
  Quiet(Quiet &&) = default;
  Quiet &operator=(Quiet &&) = default;
};

struct Body {
  Body() : value_(0) {}
  ~Body() {}
  Body(const Body &) = default;
  Body &operator=(const Body &) = default;
  Body(Body &&) = default;
  Body &operator=(Body &&) = default;
  int read() const { return value_; }

private:
  int value_;
};

int read(const Body *body) { return body->read(); }
