struct Packet {
  unsigned char kind;
  unsigned char n;
};

struct Gpio {
  volatile unsigned CR;
};

static int read(void) {
  const struct Packet packet = {1, 1};
  struct Gpio gpio = {0U};
  return packet.kind != 0 ? packet.n : (int)gpio.CR;
}

int main(void) { return read(); }
