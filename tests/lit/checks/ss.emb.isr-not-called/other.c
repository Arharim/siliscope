/* ss-companion */
#if defined(__GNUC__) // GNU interrupt attribute
__attribute__((interrupt))
#endif
void handler(void) {}
