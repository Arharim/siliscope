static int pick(int x) {
  constexpr bool On = true;
  int y;
  y = 0;
  if constexpr (On) {
    y = x;
  }
  return y;
}
