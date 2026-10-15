static int use(const int values[]) {
  enum { kSized = 4 };
  int sized[kSized];
  const int deduced[] = {1, 2, 3};
  sized[0] = 1;
  return values[0] + deduced[0] + sized[0];
}

int main(void) {
  const int local[1] = {1};
  return use(local);
}
