/* Fixture for phase 1: Clang must see the interrupt attribute under GNU C. */
#if defined(__GNUC__) // GNU interrupt attribute
void systick_isr(void) __attribute__((interrupt));
#endif

struct hw_reg {
  unsigned ctrl;
  unsigned data;
} __attribute__((packed));

_Static_assert(sizeof(struct hw_reg) == 8U, "size");
_Static_assert(__builtin_offsetof(struct hw_reg, ctrl) == 0U, "ctrl");
_Static_assert(__builtin_offsetof(struct hw_reg, data) == 4U, "data");

void systick_isr(void) {}
