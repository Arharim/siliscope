/* ss-run: profile=style expect=ss.emb.thread-naming */
void worker(void *arg);

void make_task(void (*entry)(void *));

void start(void)
{
    make_task(worker);
}

void worker(void *arg)
{
    (void)arg;
}
