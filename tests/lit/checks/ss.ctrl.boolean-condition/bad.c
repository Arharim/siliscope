static int bad(int n, const int *p) {
  if (n) {
    return 1;
  }
  while (p) {
    return 2;
  }
  return 0;
}

int main(void) {
  const int x = 1;
  return bad(1, &x);
}
