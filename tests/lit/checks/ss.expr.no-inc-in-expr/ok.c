static void f(int x) {
  enum { kLimit = 3 };
  ++x;
  x++;
  --x;
  x--;
  for (x = 0; x < kLimit; ++x) {
    /* the increment is the for-clause, not an expression statement */
  }
}
