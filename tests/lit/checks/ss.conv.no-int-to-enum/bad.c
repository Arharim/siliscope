enum State { Off, On };

static enum State bad(int n) { return (enum State)n; }
