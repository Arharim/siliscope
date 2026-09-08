void __disable_irq(void);
void __enable_irq(void);
void __set_PRIMASK(unsigned m);
void __set_BASEPRI(unsigned m);

void blind(void) {
  __disable_irq();
  __enable_irq();
}

void early(int x) {
  __disable_irq();
  if (x) {
    return;
  }
  __set_PRIMASK(0);
}

void enable_only(void) {
  __enable_irq();
}

void basepri_open(void) {
  __set_BASEPRI(0x40u);
}
