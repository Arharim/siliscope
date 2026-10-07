static void f(int x) {
  if (x) {
    x = 1;
  } else if (x == 2) {
    x = 2;
  } else {
    x = 0;
  }
  while (x) {
    --x;
  }
  do {
    --x;
  } while (x);
  for (x = 0; x < 1; ++x) {
    if (x == 0) {
      break;
    }
  }
  switch (x) {
  default:
    break;
  }
}
