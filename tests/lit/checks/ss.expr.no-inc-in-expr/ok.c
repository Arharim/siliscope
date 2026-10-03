static void f(int x) {
  ++x;
  x++;
  --x;
  x--;
  for (x = 0; x < 3; ++x) {
    /* the increment is the for-clause, not an expression statement */
  }
}
