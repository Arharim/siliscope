/* wire */
union Packet {
  unsigned raw;
  unsigned char byte;
};

static unsigned read(void) {
  union Packet packet;
  packet.raw = 1U;
  return packet.raw;
}
