#ifndef LUAMD_MATH_H
#define LUAMD_MATH_H

/* Declarations only -- deliberately unimplemented.
 *
 * fm-operator.c, psg.c and fm.c all include <math.h>, which is why this file
 * has to exist. None of them actually call libm: both FM_Operator_Constant_-
 * Initialise and PSG_Constant_Initialise sit inside '#if 0', because upstream
 * already ships their sine, power and volume tables as precomputed static
 * const arrays. Verified by preprocessing all four candidate files and
 * grepping the output -- not one call survives.
 *
 * That is worth the paragraph, because the obvious reading of the source says
 * the opposite: grep finds sin/log/pow in fm-operator.c and pow in psg.c, and
 * a freestanding port that trusts grep ends up writing a libm it never needs.
 *
 * Leaving these undeclared-but-unimplemented is the point. If a future core
 * update starts calling one, the link fails with an undefined symbol, which is
 * exactly the signal we want. A stub returning 0.0 would instead produce a
 * silently detuned FM table and hours of chasing bad audio. */

#define M_PI 3.14159265358979323846

double fabs(double x);
double floor(double x);
double ceil(double x);
double sqrt(double x);
double exp(double x);
double log(double x);
double log10(double x);
double sin(double x);
double cos(double x);
double pow(double base, double exponent);

#endif
