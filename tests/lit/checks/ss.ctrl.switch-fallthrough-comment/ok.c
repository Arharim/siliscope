static void commented(int value) {
  switch (value) {
  case 1:
    value = 1;
    /* fall through */
  case 2:
    value = 2;
    // falls through
  case 3:
    value = 3;
    /* FALLTHRU */
  case 4:
    value = 4;
    /* fall-through */
  case 5:
    value = 5;
    // fallthrough
  default:
    break;
  }
}

static void annotated(int value) {
  switch (value) {
  case 1:
    value = 1;
    __attribute__((fallthrough));
  default:
    break;
  }
}

static void inside_braces(int value) {
  switch (value) {
  case 1: {
    value = 1;
    /* fall through */
  }
  case 2:
    break;
  default:
    break;
  }
}

static void comment_before_semi(int value) {
  switch (value) {
  case 1:
    value = 1 /* fall through */;
  default:
    break;
  }
}

static void only_breaks(int value) {
  switch (value) {
  case 1:
    break;
  default:
    break;
  }
}
