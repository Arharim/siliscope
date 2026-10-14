static int flag(int n) {
  const _Bool on = n != 0;
  if (on) {
    return 1;
  }
  return 0;
}

int main(void) {
  return flag(1);
}
