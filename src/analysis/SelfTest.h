#pragma once

namespace vibecheck
{
/** Runs the measurement code against signals whose answers are known in advance and prints a
    pass/fail report. Returns the number of failures.

    Analysis code that is only ever pointed at real plugins cannot be checked: a plot looks
    plausible whether or not the maths behind it is right. These cases have arithmetic answers. */
int runSelfTest();
} // namespace vibecheck
