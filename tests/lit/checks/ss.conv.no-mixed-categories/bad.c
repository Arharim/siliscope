/* ss-run: profile=strict expect=ss.conv.no-mixed-categories */
enum State { kOff, kOn };

static int tick(enum State state, char text, _Bool flag) {
  return (state + 1) + (text + 1) + (flag << 1);
}
