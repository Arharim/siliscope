char *strcpy(char *, const char *);
char *strcat(char *, const char *);
int sprintf(char *, const char *, ...);
char *gets(char *);
char *strtok(char *, const char *);

void f(void) {
  char dst[8];
  char src[8];
  strcpy(dst, src);
  strcat(dst, src);
  sprintf(dst, "%s", src);
  gets(dst);
  strtok(dst, " ");
}
