static int read(const int *value) {
  return *value;
}

int main(void) {
  const int value = 1;
  return read(&value);
}
