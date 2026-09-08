int atoi(const char *);
double atof(const char *);
long atol(const char *);
long long atoll(const char *);

void f(void) {
  (void)atoi("1");
  (void)atof("1");
  (void)atol("1");
  (void)atoll("1");
}
