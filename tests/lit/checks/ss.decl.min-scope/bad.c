static int only;

static int use(void) {
  const int inner = 0;
  if (only != 0) {
    return inner;
  }
  return 0;
}

int main(void) { return use(); }
