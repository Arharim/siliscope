enum { kBound = 8, kCmd = 16 };

static int delay_ms(int ticks) { return ticks; }

static int run(int n) {
  int buf[kBound];
  buf[0] = 1;
  struct Msg {
    unsigned char cmd;
  };
  const struct Msg msg = {.cmd = kCmd};
  if (n == kBound) {
    return delay_ms(kBound) + buf[0] + msg.cmd;
  }
  return 0;
}

int main(void) { return run(1); }
