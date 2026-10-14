struct Pair {
  int a;
  int b;
};

static int use(void) {
  const int row[3] = {1};
  const struct Pair pair = {4};
  const int grid[2][2] = {1, 2, 3, 4};
  const int sized[] = {[1] = 4};
  return row[0] + pair.a + grid[0][0] + sized[1];
}

int main(void) { return use(); }
