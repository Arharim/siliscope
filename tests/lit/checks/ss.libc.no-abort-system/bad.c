void abort(void);
void exit(int);
char *getenv(const char *);
int system(const char *);

void f_abort(void) {
  abort();
}

void f_exit(void) {
  exit(1);
}

void f_env(void) {
  (void)getenv("x");
  (void)system("x");
}
