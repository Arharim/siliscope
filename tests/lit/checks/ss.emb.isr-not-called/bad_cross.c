/* ss-run: also=other.c expect=ss.emb.isr-not-called */
void handler(void);

void thread(void) { handler(); }
