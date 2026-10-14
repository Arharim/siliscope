static void f(int x) {
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
  } else if (x == 2) {
    x = 2;
  } else {
    x = 0;
  }
}
