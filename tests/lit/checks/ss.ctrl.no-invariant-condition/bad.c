static int never(int x) {
  if (0) {
    return x;
  }
  return 1;
}

static int always(int x) {
  if (1) {
    return x;
  }
  return 0;
}
