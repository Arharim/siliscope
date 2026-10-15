static int pick(int a, int b) {
  if (a != 0) {
    if (b != 0) {
      return 1;
    }
  }
  return 0;
}

static int chain(int a, int b, int c) {
  if (a != 0) {
    return 1;
  } else if (b != 0) {
    return 1;
  } else if (c != 0) {
    return 1;
  } else {
    return 0;
  }
}

int main(void) { return pick(1, 0) + chain(0, 1, 0); }
