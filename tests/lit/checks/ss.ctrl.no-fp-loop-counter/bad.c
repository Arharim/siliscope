static float steps(float n) {
  float i;
  float sum;
  sum = 0.0f;
  for (i = 0.0f; i < n; i = i + 1.0f) {
    sum = sum + i;
  }
  return sum;
}

int main(void) { return (int)steps(3.0f); }
