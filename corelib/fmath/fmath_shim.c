/* core:fmath libm bridge. Tycho has no trig/exp/log builtins; -lm is always on
 * the link line, so these are direct calls. */
#include <math.h>
#include "../tycho.h"

double fmx_sin(double v)  { return sin(v); }
double fmx_cos(double v)  { return cos(v); }
double fmx_tan(double v)  { return tan(v); }
double fmx_asin(double v) { return asin(v); }
double fmx_acos(double v) { return acos(v); }
double fmx_atan(double v) { return atan(v); }
double fmx_atan2(double y, double x) { return atan2(y, x); }
double fmx_sinh(double v) { return sinh(v); }
double fmx_cosh(double v) { return cosh(v); }
double fmx_tanh(double v) { return tanh(v); }
double fmx_exp(double v)  { return exp(v); }
double fmx_log(double v)  { return log(v); }
double fmx_log2(double v) { return log2(v); }
double fmx_log10(double v){ return log10(v); }
double fmx_hypot(double a, double b) { return hypot(a, b); }
