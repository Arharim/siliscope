/* ss-run: profile=embedded-cpp expect=ss.cpp.enum-class */
enum Color { Red, Green };

enum { Limit = 4 };

int code(Color color) {
  if (color == Red) {
    return 1;
  }
  return Limit;
}
