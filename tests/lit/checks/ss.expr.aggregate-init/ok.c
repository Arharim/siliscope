struct Pair {
  int a;
  int b;
};

static int use(void) {
  const int row[3] = {1, 2, 3};
  const struct Pair pair = {4, 5};
  const int grid[2][2] = {{1, 2}, {3, 4}};
  const int designated[4] = {[1] = 4, [0] = 1, [2] = 2, [3] = 3};
  const char text[4] = "abc";
  return row[0] + pair.a + grid[0][0] + designated[1] + text[0];
}

int main(void) { return use(); }
