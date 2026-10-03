#define BAD(x) #x ## suffix
#define REV(x) prefix ## #x

static int use(void) {
  return 0;
}
