static int count(int n, _Bool flag) {
  int total = 0;
  if (flag) {
    total = total + 1;
  }
  return total + n;
}

int main(void) {
  return count(1, 1);
}
