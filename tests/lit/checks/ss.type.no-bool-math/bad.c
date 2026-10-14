static int count(_Bool flag) {
  int total = flag + 1;
  flag++;
  return total + flag;
}

int main(void) {
  return count(1);
}
