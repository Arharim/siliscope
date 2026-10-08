unsigned __get_PRIMASK(void);
void __set_PRIMASK(unsigned m);
void __disable_irq(void);
void flash_erase(unsigned sector, int pages);

static void f(void) {
  const unsigned m = __get_PRIMASK();
  __disable_irq();
  flash_erase(1U, 1);
  __set_PRIMASK(m);
}
