extern int missing[];

static int read(void) {
  return missing[0];
}

int main(void) {
  return read();
}
