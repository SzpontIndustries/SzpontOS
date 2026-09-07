#ifndef _ASSERT_H
#define _ASSERT_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef NDEBUG
#define assert(ignore) ((void)0)
#else
#define assert(expr) ((expr) ? (void)0 : __assert_fail(#expr, __FILE__, __LINE__, __func__))
#endif

void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function)
    __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif /* _ASSERT_H */
