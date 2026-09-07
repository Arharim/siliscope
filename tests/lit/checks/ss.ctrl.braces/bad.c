void f(int x) {
  if (x)
    x = 1;
  else
    x = 0;
  while (x)
    --x;
  for (;;)
    break;
}
