static int read(void) {
  enum None { Left, Right };
  enum First { Start = 2, Next, Last };
  enum All { A = 1, B = 2, C = 3 };
  return Left + Start + A;
}

int main(void) {
  return read();
}
