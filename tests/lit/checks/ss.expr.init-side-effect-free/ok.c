static int fill(void) {
  enum { kRow = 2 };
  const int row[kRow] = {1, 2};
  return row[0] + row[1];
}

int main(void) { return fill(); }
