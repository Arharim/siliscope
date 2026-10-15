struct Pair {
  int a;
  int b;
};

static int use(void) {
  enum { kRow = 3, kGrid = 2, kSlots = 4 };
  const int row[kRow] = {1, 2, 3};
  const struct Pair pair = {4, 5};
  const int grid[kGrid][kGrid] = {{1, 2}, {3, 4}};
  const int designated[kSlots] = {[1] = 4, [0] = 1, [2] = 2, [3] = 3};
  const char text[kSlots] = "abc";
  return row[0] + pair.a + grid[0][0] + designated[1] + (text[0] == 'a');
}

int main(void) { return use(); }
