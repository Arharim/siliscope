void f(int x) {
  if (x == 1) {
    x = 2;
  }
  while (x) {
    --x;
  }
  do {
    --x;
  } while (x);
  for (x = 0; x < 3; ++x) {
  }
  switch (x) {
  default:
    break;
  }
}
