static float add(float a, float b) { return a + b; }

static float checked(float a, float b) {
  if (__builtin_isfinite(a / b)) {
    return a / b;
  }
  return 0.0f;
}
