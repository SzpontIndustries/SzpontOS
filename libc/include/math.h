#ifndef _MATH_H
#define _MATH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Constants */
#define HUGE_VAL (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define HUGE_VALL (__builtin_huge_vall())
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))

#define M_E 2.71828182845904523536028747135266250        /* e */
#define M_LOG2E 1.44269504088896340735992468100189214    /* log_2 e */
#define M_LOG10E 0.434294481903251827651128918916605082  /* log_10 e */
#define M_LN2 0.693147180559945309417232121458176568     /* log_e 2 */
#define M_LN10 2.30258509299404568401799145468436421     /* log_e 10 */
#define M_PI 3.14159265358979323846264338327950288       /* pi */
#define M_PI_2 1.57079632679489661923132169163975144     /* pi/2 */
#define M_PI_4 0.785398163397448309615660845819875721    /* pi/4 */
#define M_1_PI 0.318309886183790671537767526745028724    /* 1/pi */
#define M_2_PI 0.636619772367581343075535053490057448    /* 2/pi */
#define M_2_SQRTPI 1.12837916709551257389615890312154517 /* 2/sqrt(pi) */
#define M_SQRT2 1.41421356237309504880168872420969808    /* sqrt(2) */
#define M_SQRT1_2 0.707106781186547524400844362104849039 /* 1/sqrt(2) */

#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4

#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define isnormal(x) __builtin_isnormal(x)
#define signbit(x) __builtin_signbit(x)
#define isunordered(u, v) __builtin_isunordered(u, v)
#define isgreater(u, v) __builtin_isgreater(u, v)
#define isless(u, v) __builtin_isless(u, v)
#define isgreaterequal(u, v) __builtin_isgreaterequal(u, v)
#define islessequal(u, v) __builtin_islessequal(u, v)
#define islessgreater(u, v) __builtin_islessgreater(u, v)

/* Trigonometric functions */
double sin(double x);
double cos(double x);
double tan(double x);
void sincos(double x, double *s, double *c);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);

/* Hyperbolic functions */
double sinh(double x);
double cosh(double x);
double tanh(double x);
double asinh(double x);
double acosh(double x);
double atanh(double x);

/* Exponential and logarithmic functions */
double exp(double x);
double exp2(double x);
double expm1(double x);
double log(double x);
double log2(double x);
double log10(double x);
double log1p(double x);
double logb(double x);
int ilogb(double x);

/* Power and root functions */
double pow(double base, double exp);
double sqrt(double x);
double cbrt(double x);
double hypot(double x, double y);
double fabs(double x);

/* Rounding and remainder */
double ceil(double x);
double floor(double x);
double trunc(double x);
double round(double x);
long lround(double x);
long long llround(double x);
double rint(double x);
long lrint(double x);
long long llrint(double x);
double nearbyint(double x);
double fmod(double x, double y);
double remainder(double x, double y);
double remquo(double x, double y, int *quo);

/* Floating-point manipulation */
double copysign(double x, double y);
double nan(const char *tagp);
double nextafter(double x, double y);
double nexttoward(double x, long double y);
double fdim(double x, double y);
double fmax(double x, double y);
double fmin(double x, double y);
double fma(double x, double y, double z);
double ldexp(double x, int exp);
double frexp(double x, int *exp);
double modf(double x, double *iptr);
double scalbn(double x, int n);
double scalbln(double x, long n);

/* Single precision (float) variants */
float sinf(float x);
float cosf(float x);
float tanf(float x);
float asinf(float x);
float acosf(float x);
float atanf(float x);
float atan2f(float y, float x);
float sinhf(float x);
float coshf(float x);
float tanhf(float x);
float asinhf(float x);
float acoshf(float x);
float atanhf(float x);
float expf(float x);
float exp2f(float x);
float expm1f(float x);
float logf(float x);
float log2f(float x);
float log10f(float x);
float log1pf(float x);
float powf(float base, float exp);
float sqrtf(float x);
float cbrtf(float x);
float hypotf(float x, float y);
float fabsf(float x);
float ceilf(float x);
float floorf(float x);
float truncf(float x);
float roundf(float x);
long lroundf(float x);
long long llroundf(float x);
float rintf(float x);
long lrintf(float x);
long long llrintf(float x);
float fmodf(float x, float y);
float remainderf(float x, float y);
float copysignf(float x, float y);
float ldexpf(float x, int exp);
float frexpf(float x, int *exp);
float modff(float x, float *iptr);

long double frexpl(long double x, int *exp);
long double modfl(long double x, long double *iptr);

double tgamma(double x);
float tgammaf(float x);
long double tgammal(long double x);
double lgamma(double x);
float lgammaf(float x);
long double lgammal(long double x);

#ifdef __cplusplus
}
#endif

#endif /* _MATH_H */
