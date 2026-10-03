/* ss-run: profile=embedded-cpp expect=clean */
enum class Color { Red, Green };

int code(Color color) {
  if (color == Color::Red) {
    return 1;
  }
  return 0;
}
