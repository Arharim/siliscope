void lock_a(void);
void lock_b(void);
void unlock_a(void);
void unlock_b(void);

static void first(void) {
  lock_a();
  lock_b();
  unlock_b();
  unlock_a();
}

static void second(void) {
  lock_b();
  lock_a();
  unlock_a();
  unlock_b();
}

int main(void) {
  first();
  second();
  return 0;
}
