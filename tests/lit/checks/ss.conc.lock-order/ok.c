void lock_a(void);
void lock_b(void);
void unlock_a(void);
void unlock_b(void);

static void nested(void) {
  lock_a();
  lock_b();
  unlock_b();
  unlock_a();
}

static void sequential(void) {
  lock_a();
  unlock_a();
  lock_b();
  unlock_b();
}

int main(void) {
  nested();
  sequential();
  return 0;
}
