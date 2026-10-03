static void annotated(int value) {
  switch (value) {
  case 1:
    value = 1;
    [[fallthrough]];
  case 2:
    throw value;
  default:
    break;
  }
}
