static void tick(int x) {
  for (;;) {
    if (x != 0) {
      goto out;
    }
    goto out;
  }
out:
  return;
}
