#define and &&

static int use(int left, int right) {
  if (left and right) {
    return 1;
  }
  return 0;
}
