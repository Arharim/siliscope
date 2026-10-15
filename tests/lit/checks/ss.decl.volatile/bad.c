static volatile int gate;

static void take(void) {
  if (gate == 0) {
    gate = 1;
  }
}

int main(void) {
  take();
  return 0;
}
