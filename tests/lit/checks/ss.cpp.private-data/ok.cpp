/* ss-run: profile=embedded-cpp allow=ss.type.hw-layout:Reg expect=clean */
struct Point {
  int x;
  int y;
};

struct Reg {
  volatile unsigned value;
};

struct Box {
  explicit Box(int raw) : value_(raw) {}
  int read() const { return value_; }

private:
  int value_;
};

int read_point(const Point *point) {
  if (point == nullptr) {
    return 0;
  }
  return point->x + point->y;
}
