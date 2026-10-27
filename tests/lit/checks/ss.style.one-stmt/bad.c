/* ss-run: profile=style expect=ss.style.one-stmt */
void tick(void)
{
    int a = 1; int b = 2;
    (void)a;
    (void)b;
}
