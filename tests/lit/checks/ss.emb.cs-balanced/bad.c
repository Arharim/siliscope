void taskENTER_CRITICAL(void);
void taskEXIT_CRITICAL(void);

void early(int x) {
  taskENTER_CRITICAL();
  if (x) {
    return;
  }
  taskEXIT_CRITICAL();
}

void missing(void) {
  taskENTER_CRITICAL();
}

void extra(void) {
  taskEXIT_CRITICAL();
}
