static int fill(void) {
  const int row[2] = {1, 2};
  return row[0] + row[1];
}

int main(void) { return fill(); }
