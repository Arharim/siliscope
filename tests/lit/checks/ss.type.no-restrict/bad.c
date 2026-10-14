static int read(const int *restrict value) {
  return *value;
}

int main(void) {
  const int value = 1;
  return read(&value);
}
