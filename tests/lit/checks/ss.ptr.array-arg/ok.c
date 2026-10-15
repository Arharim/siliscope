enum { kFour = 4, kEight = 8 };

static int sum4(const int a[kFour]) { return a[0] + a[1] + a[2] + a[3]; }

static int take_ptr(const int *p) { return p[0]; }

static int take_str(const char a[kFour]) { return a[0]; }

static int call_ok(void) {
  int buf[kFour];
  int big[kEight];
  buf[0] = 1;
  buf[1] = 2;
  buf[2] = 3;
  buf[3] = 4;
  big[0] = 5;
  const int *const p = buf;
  return sum4(buf) + sum4(big) + take_ptr(p) + take_str("abc");
}
