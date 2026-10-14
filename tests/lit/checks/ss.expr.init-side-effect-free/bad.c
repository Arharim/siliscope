static int bump(void) { return 1; }

static int fill(int n) {
  const int row[2] = {n++, bump()};
  return row[0] + row[1];
}

int main(void) { return fill(1); }
