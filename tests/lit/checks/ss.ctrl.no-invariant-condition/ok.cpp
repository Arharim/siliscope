static constexpr bool On = true;

static int pick(int x) {
  int y;
  y = 0;
  if constexpr (On) {
    y = x;
  }
  return y;
}
