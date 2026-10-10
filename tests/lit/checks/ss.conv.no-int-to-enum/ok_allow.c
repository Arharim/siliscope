/* ss-run: allow=ss.conv.no-int-to-enum:from_bus expect=clean */
enum State { Off, On };

static enum State from_bus(int n) { return (enum State)n; }
