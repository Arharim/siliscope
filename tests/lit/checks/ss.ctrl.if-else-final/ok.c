static void f(int x) {
  enum { kTwo = 2 };
  if (x != 0) {
    x = 1;
  }
  if (x != 0) {
    x = 1;
  } else {
    x = 0;
  }
  if (x != 0) {
    x = 1;
  } else if (x == kTwo) {
    x = 2;
  } else {
    x = 0;
  }
}
