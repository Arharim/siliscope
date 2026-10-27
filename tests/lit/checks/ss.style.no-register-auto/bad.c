/* ss-run: profile=style expect=ss.style.no-register-auto */
void tick(void)
{
    register int x = 1;
    (void)x;
}
