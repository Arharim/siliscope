/* ss-run: profile=embedded-cpp expect=clean */
struct Quiet {
  ~Quiet() = default;
};

struct Body {
  int value;
  Body() : value(0) {}
  ~Body() {}
};

int read(const Body *body) { return body->value; }
