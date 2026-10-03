/* ss-run: profile=embedded-cpp expect=ss.cpp.nullptr */
#define NULL 0

struct Slot {
  int value;
};

int *from_zero() { return 0; }

int eq(const int *p) { return p == 0 ? 1 : 0; }

int *from_null() { return NULL; }

int *from_gnu() { return __null; }

int Slot::*member_zero() { return 0; }
