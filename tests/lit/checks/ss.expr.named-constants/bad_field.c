static int run(void) {
  struct Msg {
    unsigned char cmd;
  };
  struct Msg msg = {.cmd = 0x10};
  msg.cmd = msg.cmd;
  return msg.cmd;
}

int main(void) { return run(); }
