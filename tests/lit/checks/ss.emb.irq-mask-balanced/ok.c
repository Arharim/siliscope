unsigned __get_PRIMASK(void);
void __set_PRIMASK(unsigned m);
void __disable_irq(void);
unsigned __get_BASEPRI(void);
void __set_BASEPRI(unsigned m);

void primask_save(void) {
  const unsigned m = __get_PRIMASK();
  __disable_irq();
  __set_PRIMASK(m);
}

void primask_both_returns(int x) {
  const unsigned m = __get_PRIMASK();
  __disable_irq();
  if (x) {
    __set_PRIMASK(m);
    return;
  }
  __set_PRIMASK(m);
}

void basepri_save(void) {
  const unsigned b = __get_BASEPRI();
  __set_BASEPRI(0x40u);
  __set_BASEPRI(b);
}
