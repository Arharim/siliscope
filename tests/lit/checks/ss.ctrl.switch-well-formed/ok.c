static void stacked(int value) {
  switch (value) {
  case 1: /* fall through */
  case 2:
    value = 1;
    break;
  case 3: {
    value = 3;
    break;
  }
  default:
    break;
  }
}

static int returned(int value) {
  switch (value) {
  case 1:
    return value;
  default:
    return 0;
  }
}

static void comparison_is_int(int value) {
  switch (value == 1) {
  case 0:
    break;
  default:
    break;
  }
}
