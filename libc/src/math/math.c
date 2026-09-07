#include <math.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>

/* =========================================================================
 * Basic Floating-Point & Bitwise Operations
 * ========================================================================= */

double fabs(double x) {
    union {
        double d;
        uint64_t u;
    } un = {.d = x};
    un.u &= 0x7fffffffffffffffULL;
    return un.d;
}

float fabsf(float x) {
    union {
        float f;
        uint32_t u;
    } un = {.f = x};
    un.u &= 0x7fffffffU;
    return un.f;
}

double copysign(double x, double y) {
    union {
        double d;
        uint64_t u;
    } ux = {.d = x}, uy = {.d = y};
    ux.u = (ux.u & 0x7fffffffffffffffULL) | (uy.u & 0x8000000000000000ULL);
    return ux.d;
}

float copysignf(float x, float y) {
    union {
        float f;
        uint32_t u;
    } ux = {.f = x}, uy = {.f = y};
    ux.u = (ux.u & 0x7fffffffU) | (uy.u & 0x80000000U);
    return ux.f;
}

double nan(const char *tagp) {
    (void)tagp;
    return NAN;
}

/* =========================================================================
 * Rounding, Truncation & Remainder
 * ========================================================================= */

double floor(double x) {
    if (isnan(x) || isinf(x))
        return x;
    long long n = (long long)x;
    if (x < 0.0 && x != (double)n)
        n--;
    return (double)n;
}

float floorf(float x) {
    return (float)floor((double)x);
}

double ceil(double x) {
    if (isnan(x) || isinf(x))
        return x;
    long long n = (long long)x;
    if (x > 0.0 && x != (double)n)
        n++;
    return (double)n;
}

float ceilf(float x) {
    return (float)ceil((double)x);
}

double trunc(double x) {
    if (isnan(x) || isinf(x))
        return x;
    return (double)((long long)x);
}

float truncf(float x) {
    return (float)trunc((double)x);
}

double round(double x) {
    if (isnan(x) || isinf(x))
        return x;
    return (x >= 0.0) ? floor(x + 0.5) : ceil(x - 0.5);
}

float roundf(float x) {
    return (float)round((double)x);
}

long lround(double x) {
    return (long)round(x);
}

long long llround(double x) {
    return (long long)round(x);
}

long lroundf(float x) {
    return (long)roundf(x);
}

long long llroundf(float x) {
    return (long long)roundf(x);
}

double rint(double x) {
    return round(x);
}

float rintf(float x) {
    return (float)rint((double)x);
}

long lrint(double x) {
    return (long)rint(x);
}

long long llrint(double x) {
    return (long long)rint(x);
}

long lrintf(float x) {
    return (long)rintf(x);
}

long long llrintf(float x) {
    return (long long)rintf(x);
}

double nearbyint(double x) {
    return round(x);
}

double fmod(double x, double y) {
    if (y == 0.0 || isnan(x) || isnan(y) || isinf(x)) {
        errno = EDOM;
        return NAN;
    }
    if (isinf(y))
        return x;
    double quotient = trunc(x / y);
    return x - quotient * y;
}

float fmodf(float x, float y) {
    return (float)fmod((double)x, (double)y);
}

double remainder(double x, double y) {
    if (y == 0.0 || isnan(x) || isnan(y) || isinf(x))
        return NAN;
    double quotient = round(x / y);
    return x - quotient * y;
}

float remainderf(float x, float y) {
    return (float)remainder((double)x, (double)y);
}

double remquo(double x, double y, int *quo) {
    if (y == 0.0 || isnan(x) || isnan(y) || isinf(x)) {
        if (quo)
            *quo = 0;
        return NAN;
    }
    double q = round(x / y);
    if (quo)
        *quo = (int)((long long)q & 0x7fffffff);
    return x - q * y;
}

/* =========================================================================
 * Exponent Manipulation & Scaling
 * ========================================================================= */

double ldexp(double x, int exp) {
    if (x == 0.0 || isnan(x) || isinf(x) || exp == 0)
        return x;
    double factor = 1.0;
    int e = exp;
    if (e > 0) {
        while (e >= 30) {
            factor *= (double)(1ULL << 30);
            e -= 30;
        }
        while (e > 0) {
            factor *= 2.0;
            e--;
        }
        return x * factor;
    } else {
        while (e <= -30) {
            factor *= (1.0 / (double)(1ULL << 30));
            e += 30;
        }
        while (e < 0) {
            factor /= 2.0;
            e++;
        }
        return x * factor;
    }
}

float ldexpf(float x, int exp) {
    return (float)ldexp((double)x, exp);
}

double scalbn(double x, int n) {
    return ldexp(x, n);
}

double scalbln(double x, long n) {
    return ldexp(x, (int)n);
}

double frexp(double x, int *exp) {
    if (x == 0.0) {
        if (exp)
            *exp = 0;
        return 0.0;
    }
    if (isnan(x) || isinf(x)) {
        if (exp)
            *exp = 0;
        return x;
    }
    int e = 0;
    double sign = (x < 0.0) ? -1.0 : 1.0;
    x = fabs(x);
    while (x >= 1.0) {
        x /= 2.0;
        e++;
    }
    while (x < 0.5) {
        x *= 2.0;
        e--;
    }
    if (exp)
        *exp = e;
    return sign * x;
}

float frexpf(float x, int *exp) {
    return (float)frexp((double)x, exp);
}

long double frexpl(long double x, int *exp) {
    return (long double)frexp((double)x, exp);
}

double modf(double x, double *iptr) {
    if (isnan(x) || isinf(x)) {
        if (iptr)
            *iptr = x;
        return 0.0;
    }
    double i = trunc(x);
    if (iptr)
        *iptr = i;
    return x - i;
}

float modff(float x, float *iptr) {
    double d;
    float r = (float)modf((double)x, &d);
    if (iptr)
        *iptr = (float)d;
    return r;
}

long double modfl(long double x, long double *iptr) {
    double d;
    long double r = (long double)modf((double)x, &d);
    if (iptr)
        *iptr = (long double)d;
    return r;
}

/* =========================================================================
 * Square Root & Powers
 * ========================================================================= */

double sqrt(double x) {
    if (x < 0.0) {
        errno = EDOM;
        return NAN;
    }
    if (isnan(x) || isinf(x) || x == 0.0)
        return x;
    double res;
    __asm__("sqrtsd %1, %0" : "=x"(res) : "x"(x));
    return res;
}

float sqrtf(float x) {
    if (x < 0.0f) {
        errno = EDOM;
        return NAN;
    }
    if (isnan(x) || isinf(x) || x == 0.0f)
        return x;
    float res;
    __asm__("sqrtss %1, %0" : "=x"(res) : "x"(x));
    return res;
}

double cbrt(double x) {
    if (x == 0.0 || isnan(x) || isinf(x))
        return x;
    double sign = (x < 0.0) ? -1.0 : 1.0;
    return sign * pow(fabs(x), 1.0 / 3.0);
}

float cbrtf(float x) {
    return (float)cbrt((double)x);
}

double hypot(double x, double y) {
    if (isinf(x) || isinf(y))
        return HUGE_VAL;
    if (isnan(x) || isnan(y))
        return NAN;
    x = fabs(x);
    y = fabs(y);
    if (x == 0.0)
        return y;
    if (y == 0.0)
        return x;
    double max = (x > y) ? x : y;
    double min = (x > y) ? y : x;
    double r = min / max;
    return max * sqrt(1.0 + r * r);
}

float hypotf(float x, float y) {
    return (float)hypot((double)x, (double)y);
}

/* =========================================================================
 * Trigonometric Functions
 * ========================================================================= */

void sincos(double x, double *s, double *c) {
    if (isnan(x) || isinf(x)) {
        if (s)
            *s = NAN;
        if (c)
            *c = NAN;
        return;
    }
    if (fabs(x) > M_PI) {
        x = remainder(x, 2.0 * M_PI);
    }
    double sv, cv;
    __asm__ volatile("fsincos" : "=t"(cv), "=u"(sv) : "0"(x));
    if (s)
        *s = sv;
    if (c)
        *c = cv;
}

double sin(double x) {
    if (isnan(x))
        return NAN;
    if (isinf(x)) {
        errno = EDOM;
        return NAN;
    }
    if (fabs(x) > M_PI)
        x = remainder(x, 2.0 * M_PI);
    double res;
    __asm__ volatile("fsin" : "=t"(res) : "0"(x));
    return res;
}

float sinf(float x) {
    return (float)sin((double)x);
}

double cos(double x) {
    if (isnan(x))
        return NAN;
    if (isinf(x)) {
        errno = EDOM;
        return NAN;
    }
    if (fabs(x) > M_PI)
        x = remainder(x, 2.0 * M_PI);
    double res;
    __asm__ volatile("fcos" : "=t"(res) : "0"(x));
    return res;
}

float cosf(float x) {
    return (float)cos((double)x);
}

double tan(double x) {
    if (isnan(x))
        return NAN;
    if (isinf(x)) {
        errno = EDOM;
        return NAN;
    }
    double s, c;
    sincos(x, &s, &c);
    if (c == 0.0) {
        errno = ERANGE;
        return (s >= 0.0) ? HUGE_VAL : -HUGE_VAL;
    }
    return s / c;
}

float tanf(float x) {
    return (float)tan((double)x);
}

double atan2(double y, double x) {
    if (isnan(x) || isnan(y))
        return NAN;
    double res;
    __asm__ volatile("fpatan" : "=t"(res) : "0"(x), "u"(y) : "st(1)");
    return res;
}

float atan2f(float y, float x) {
    return (float)atan2((double)y, (double)x);
}

double atan(double x) {
    return atan2(x, 1.0);
}

float atanf(float x) {
    return (float)atan((double)x);
}

double asin(double x) {
    if (x < -1.0 || x > 1.0) {
        errno = EDOM;
        return NAN;
    }
    if (x == 1.0)
        return M_PI_2;
    if (x == -1.0)
        return -M_PI_2;
    return atan2(x, sqrt(1.0 - x * x));
}

float asinf(float x) {
    return (float)asin((double)x);
}

double acos(double x) {
    if (x < -1.0 || x > 1.0) {
        errno = EDOM;
        return NAN;
    }
    if (x == 1.0)
        return 0.0;
    if (x == -1.0)
        return M_PI;
    return atan2(sqrt(1.0 - x * x), x);
}

float acosf(float x) {
    return (float)acos((double)x);
}

/* =========================================================================
 * Exponential and Logarithmic Functions
 * ========================================================================= */

double log2(double x) {
    if (x < 0.0) {
        errno = EDOM;
        return NAN;
    }
    if (x == 0.0) {
        errno = ERANGE;
        return -HUGE_VAL;
    }
    if (isnan(x) || isinf(x))
        return x;
    double res;
    double one = 1.0;
    __asm__ volatile("fyl2x" : "=t"(res) : "0"(x), "u"(one) : "st(1)");
    return res;
}

float log2f(float x) {
    return (float)log2((double)x);
}

double log(double x) {
    return log2(x) * M_LN2;
}

float logf(float x) {
    return (float)log((double)x);
}

double log10(double x) {
    return log2(x) * 0.301029995663981195213738894724493026768; /* log10(2) */
}

float log10f(float x) {
    return (float)log10((double)x);
}

double log1p(double x) {
    if (x < -1.0) {
        errno = EDOM;
        return NAN;
    }
    if (x == -1.0) {
        errno = ERANGE;
        return -HUGE_VAL;
    }
    if (fabs(x) < 1e-4) {
        return x - (x * x) / 2.0 + (x * x * x) / 3.0 - (x * x * x * x) / 4.0;
    }
    return log(1.0 + x);
}

float log1pf(float x) {
    return (float)log1p((double)x);
}

double logb(double x) {
    if (x == 0.0)
        return -HUGE_VAL;
    if (isnan(x))
        return NAN;
    if (isinf(x))
        return HUGE_VAL;
    int exp;
    frexp(x, &exp);
    return (double)(exp - 1);
}

int ilogb(double x) {
    return (int)logb(x);
}

double exp(double x) {
    if (isnan(x))
        return NAN;
    if (x > 709.78271289338399) {
        errno = ERANGE;
        return HUGE_VAL;
    }
    if (x < -745.1332191019412) {
        return 0.0;
    }
    if (x == 0.0)
        return 1.0;

    double val = x * M_LOG2E;
    int k = (int)round(val);
    double f = val - (double)k;

    double res;
    __asm__ volatile("f2xm1\n\t"
                     "fld1\n\t"
                     "faddp"
                     : "=t"(res)
                     : "0"(f));

    return ldexp(res, k);
}

float expf(float x) {
    return (float)exp((double)x);
}

double exp2(double x) {
    if (isnan(x))
        return NAN;
    if (x > 1023.0)
        return HUGE_VAL;
    if (x < -1074.0)
        return 0.0;
    int k = (int)round(x);
    double f = x - (double)k;
    double res;
    __asm__ volatile("f2xm1\n\t"
                     "fld1\n\t"
                     "faddp"
                     : "=t"(res)
                     : "0"(f));
    return ldexp(res, k);
}

float exp2f(float x) {
    return (float)exp2((double)x);
}

double expm1(double x) {
    if (fabs(x) < 1e-5) {
        return x + (x * x) / 2.0 + (x * x * x) / 6.0;
    }
    return exp(x) - 1.0;
}

float expm1f(float x) {
    return (float)expm1((double)x);
}

/* =========================================================================
 * Power Function (pow)
 * ========================================================================= */

double pow(double x, double y) {
    if (y == 0.0)
        return 1.0;
    if (isnan(x) || isnan(y))
        return NAN;
    if (x == 1.0)
        return 1.0;
    if (x == 0.0) {
        if (y < 0.0) {
            errno = ERANGE;
            return HUGE_VAL;
        }
        return 0.0;
    }

    long long iy = (long long)y;
    if (y == (double)iy) {
        double base = (iy < 0) ? (1.0 / x) : x;
        unsigned long long exp_val = (iy < 0) ? (unsigned long long)(-iy) : (unsigned long long)iy;
        double res = 1.0;
        while (exp_val > 0) {
            if (exp_val & 1)
                res *= base;
            base *= base;
            exp_val >>= 1;
        }
        return res;
    }

    if (x < 0.0) {
        errno = EDOM;
        return NAN;
    }

    return exp(y * log(x));
}

float powf(float base, float exp) {
    return (float)pow((double)base, (double)exp);
}

/* =========================================================================
 * Hyperbolic Functions
 * ========================================================================= */

double sinh(double x) {
    if (isnan(x))
        return NAN;
    if (fabs(x) > 710.0)
        return (x > 0) ? HUGE_VAL : -HUGE_VAL;
    double ex = exp(x);
    double emx = exp(-x);
    return (ex - emx) / 2.0;
}

float sinhf(float x) {
    return (float)sinh((double)x);
}

double cosh(double x) {
    if (isnan(x))
        return NAN;
    if (fabs(x) > 710.0)
        return HUGE_VAL;
    double ex = exp(x);
    double emx = exp(-x);
    return (ex + emx) / 2.0;
}

float coshf(float x) {
    return (float)cosh((double)x);
}

double tanh(double x) {
    if (isnan(x))
        return NAN;
    if (x > 20.0)
        return 1.0;
    if (x < -20.0)
        return -1.0;
    double ex = exp(2.0 * x);
    return (ex - 1.0) / (ex + 1.0);
}

float tanhf(float x) {
    return (float)tanh((double)x);
}

double asinh(double x) {
    if (isnan(x) || isinf(x))
        return x;
    return log(x + sqrt(x * x + 1.0));
}

float asinhf(float x) {
    return (float)asinh((double)x);
}

double acosh(double x) {
    if (x < 1.0) {
        errno = EDOM;
        return NAN;
    }
    return log(x + sqrt(x * x - 1.0));
}

float acoshf(float x) {
    return (float)acosh((double)x);
}

double atanh(double x) {
    if (x < -1.0 || x > 1.0) {
        errno = EDOM;
        return NAN;
    }
    if (x == 1.0)
        return HUGE_VAL;
    if (x == -1.0)
        return -HUGE_VAL;
    return 0.5 * log((1.0 + x) / (1.0 - x));
}

float atanhf(float x) {
    return (float)atanh((double)x);
}

/* =========================================================================
 * Numerical Utilities (fmin, fmax, fdim, fma, nextafter)
 * ========================================================================= */

double fdim(double x, double y) {
    if (isnan(x) || isnan(y))
        return NAN;
    return (x > y) ? (x - y) : 0.0;
}

double fmax(double x, double y) {
    if (isnan(x))
        return y;
    if (isnan(y))
        return x;
    return (x > y) ? x : y;
}

double fmin(double x, double y) {
    if (isnan(x))
        return y;
    if (isnan(y))
        return x;
    return (x < y) ? x : y;
}

double fma(double x, double y, double z) {
    return (x * y) + z;
}

double nextafter(double x, double y) {
    if (isnan(x) || isnan(y))
        return NAN;
    if (x == y)
        return y;
    union {
        double d;
        uint64_t u;
    } ux = {.d = x};
    if (x == 0.0) {
        ux.u = 1ULL;
        return (y > 0.0) ? ux.d : -ux.d;
    }
    if ((x > 0.0) ^ (y > x)) {
        ux.u--;
    } else {
        ux.u++;
    }
    return ux.d;
}

double nexttoward(double x, long double y) {
    return nextafter(x, (double)y);
}

static const double _gamma_p[] = {
    676.5203681218851,
    -1259.1392167224028,
    771.32342877765313,
    -176.61502916214059,
    12.507343278686905,
    -0.138571095836524,
    9.9843695780195716e-6,
    1.5056327351493116e-7
};

double tgamma(double x) {
    if (x < 0.5) {
        return 3.14159265358979323846 / (sin(3.14159265358979323846 * x) * tgamma(1.0 - x));
    }
    x -= 1.0;
    double a = 0.99999999999980993;
    double t = x + 7.5;
    for (int i = 0; i < 8; i++) {
        a += _gamma_p[i] / (x + (double)(i + 1));
    }
    return sqrt(2.0 * 3.14159265358979323846) * pow(t, x + 0.5) * exp(-t) * a;
}

float tgammaf(float x) {
    return (float)tgamma((double)x);
}

long double tgammal(long double x) {
    return (long double)tgamma((double)x);
}

double lgamma(double x) {
    double g = tgamma(x);
    return log(fabs(g));
}

float lgammaf(float x) {
    return (float)lgamma((double)x);
}

long double lgammal(long double x) {
    return (long double)lgamma((double)x);
}
