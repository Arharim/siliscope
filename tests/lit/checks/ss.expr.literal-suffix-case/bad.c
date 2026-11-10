/* ss-run: profile=style expect=ss.expr.literal-suffix-case */
#include "bad.h"

void tick(void)
{
    const unsigned value = 1u;
    const float gain = 1.0f;
    (void)value;
    (void)gain;
}
