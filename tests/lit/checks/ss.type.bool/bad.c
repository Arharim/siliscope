static int flag(int n) {
  const _Bool on = (_Bool)n;
  return on ? 1 : 0;
}

int main(void) {
  return flag(1);
}
