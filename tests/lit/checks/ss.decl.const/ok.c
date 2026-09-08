int g;

void writes(int *p) {
  *p = 1;
}

void uses_const(const int *p) {
  int x = *p;
  x = x + 1;
  (void)x;
}

void loop(int n) {
  int i;
  for (i = 0; i < n; ++i) {
  }
}

void ptr_write(void) {
  int *q = &g;
  *q = 0;
  q = &g;
}
