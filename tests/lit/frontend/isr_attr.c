/* Fixture for phase 1: Clang must see the interrupt attribute under GNU C. */
#if defined(__GNUC__)
void systick_isr(void) __attribute__((interrupt));
#endif

struct hw_reg {
  unsigned ctrl;
  unsigned data;
} __attribute__((packed));

void systick_isr(void) {}
