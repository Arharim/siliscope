static void missing_default(int value) {
  switch (value) {
  case 1:
    value = 1;
    break;
  }
}

static void boolean_cond(_Bool value) {
  switch (value) {
  case 0:
    break;
  default:
    break;
  }
}

static void nested_label(int value) {
  switch (value) {
  case 1:
    if (value) {
    case 2:
      break;
    }
    break;
  default:
    break;
  }
}

static void empty_clause(int value) {
  switch (value) {
  case 1:
    ;
  default:
    break;
  }
}

static void empty_block(int value) {
  switch (value) {
  case 0: {
  }
  default:
    break;
  }
}

static void falls_off(int value) {
  switch (value) {
  case 1:
    break;
  default:
    value = 1;
  }
}

static void not_compound(int value) {
  switch (value)
  default:
    break;
}
