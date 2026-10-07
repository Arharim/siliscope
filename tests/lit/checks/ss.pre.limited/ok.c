#include "ok.h"

// uart is optional on this board
#if defined(LIMIT_UART)
#else
#endif

#if defined(LIMIT_SPI) // spi is optional on this board
#elif defined(LIMIT_I2C) // i2c is optional on this board
#endif

static int use(void) {
  return LIMIT_OK;
}
