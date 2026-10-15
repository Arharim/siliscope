static void f(int x) {
  enum { kLimit = 3 };
  if (x == 1) {
    x = 2;
  }
  while (x != 0) {
    --x;
  }
  do {
    --x;
  } while (x != 0);
  for (x = 0; x < kLimit; ++x) {
    /* comparison in the condition */
  }
  switch (x) {
  default:
    break;
  }
}
