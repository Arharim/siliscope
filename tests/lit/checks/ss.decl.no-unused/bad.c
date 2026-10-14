static int live(void) { return 1; }

static int dead(void);

static int dead(void) { return 2; }

static int spare;

typedef int unused_units;

int main(void) { return live(); }
