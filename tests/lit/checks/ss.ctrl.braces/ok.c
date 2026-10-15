static void f(int x) {
  enum { kTwo = 2 };
  if (x != 0) {
    x = 1;
  } else if (x == kTwo) {
    x = 2;
  } else {
    x = 0;
  }
  while (x != 0) {
    --x;
  }
  do {
    --x;
  } while (x != 0);
  for (x = 0; x < 1; ++x) {
    if (x == 0) {
      break;
    }
  }
  switch (x) {
  default:
    break;
  }
}
