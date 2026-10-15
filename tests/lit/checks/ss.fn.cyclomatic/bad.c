static int busy(int a, int b, int c, int d, int e, int f, int g, int h) {
  return a && b && c && d && e && f && g && h && a && b && c && d && e && f && a && b;
}

int main(void) { return busy(1, 1, 1, 1, 1, 1, 1, 1); }
