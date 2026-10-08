/* ss-run: also=other.c expect=ss.emb.no-log-in-isr */
void helper(void);

void systick_isr(void) { helper(); }
