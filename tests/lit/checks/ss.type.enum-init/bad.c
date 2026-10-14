static int read(void) {
  enum Mixed { A = 1, B, C = 4 };
  enum Same { One = 1, Two = 1 };
  return A + One;
}

int main(void) {
  return read();
}
