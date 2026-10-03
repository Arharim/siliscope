static void bare(int value) {
  switch (value) {
  case 1:
    value = 1;
  case 2:
    break;
  default:
    break;
  }
}

static void unrelated_comment(int value) {
  switch (value) {
  case 1:
    value = 1;
    /* next */
  default:
    break;
  }
}
