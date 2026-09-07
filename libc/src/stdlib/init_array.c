/*
 * init_array.c - ELF .init_array and .fini_array execution & C++ exit handling
 * (C) Copyright by Szpont Industries. All rights reserved.
 */

#include <stddef.h>
#include <stdint.h>

/* Weak symbols provided by the GNU linker script */
extern void (*__preinit_array_start[])(void) __attribute__((weak));
extern void (*__preinit_array_end[])(void) __attribute__((weak));
extern void (*__init_array_start[])(void) __attribute__((weak));
extern void (*__init_array_end[])(void) __attribute__((weak));
extern void (*__fini_array_start[])(void) __attribute__((weak));
extern void (*__fini_array_end[])(void) __attribute__((weak));

/* Exported __dso_handle for C++ static object destruction */
void *__dso_handle __attribute__((weak)) = &__dso_handle;

/* Global array populated by the ELF loader for shared library constructors */
uintptr_t __szpont_so_init_array[64] = {0};
size_t __szpont_so_init_count = 0;

/* Run preinit and init arrays (constructors) */
void __libc_init_array(void (*init_start[])(void), void (*init_end[])(void),
                       void (*preinit_start[])(void), void (*preinit_end[])(void)) {
    /* 1. Run shared library constructors */
    for (size_t i = 0; i < __szpont_so_init_count && i < 64; i++) {
        if (__szpont_so_init_array[i]) {
            void (*fn)(void) = (void (*)(void))__szpont_so_init_array[i];
            fn();
        }
    }

    /* 2. Run executable preinit array */
    if ((uintptr_t)preinit_start >= 0x400000 && (uintptr_t)preinit_end >= 0x400000 &&
        preinit_start < preinit_end && (size_t)(preinit_end - preinit_start) < 65536) {
        size_t count = (size_t)(preinit_end - preinit_start);
        for (size_t i = 0; i < count; i++) {
            if (preinit_start[i]) {
                preinit_start[i]();
            }
        }
    } else if (__preinit_array_start && __preinit_array_end && &__preinit_array_start[0] < &__preinit_array_end[0]) {
        size_t count = (size_t)(__preinit_array_end - __preinit_array_start);
        for (size_t i = 0; i < count; i++) {
            if (__preinit_array_start[i]) {
                __preinit_array_start[i]();
            }
        }
    }

    /* 3. Run executable init array */
    if ((uintptr_t)init_start >= 0x400000 && (uintptr_t)init_end >= 0x400000 &&
        init_start < init_end && (size_t)(init_end - init_start) < 65536) {
        size_t count = (size_t)(init_end - init_start);
        for (size_t i = 0; i < count; i++) {
            if (init_start[i]) {
                init_start[i]();
            }
        }
    } else if (__init_array_start && __init_array_end && &__init_array_start[0] < &__init_array_end[0]) {
        size_t count = (size_t)(__init_array_end - __init_array_start);
        for (size_t i = 0; i < count; i++) {
            if (__init_array_start[i]) {
                __init_array_start[i]();
            }
        }
    }
}

/* Run fini array (destructors) in reverse order */
void __libc_fini_array(void) {
    if (__fini_array_start && __fini_array_end) {
        size_t count = (size_t)(__fini_array_end - __fini_array_start);
        for (size_t i = count; i > 0; i--) {
            if (__fini_array_start[i - 1]) {
                __fini_array_start[i - 1]();
            }
        }
    }
}

/* __cxa_atexit and __cxa_finalize support for C++ destructor registration */
typedef struct cxa_exit_entry {
    void (*destructor)(void *);
    void *arg;
    void *dso;
} cxa_exit_entry_t;

#define MAX_CXA_ENTRIES 512
static cxa_exit_entry_t g_cxa_entries[MAX_CXA_ENTRIES];
static size_t g_cxa_count = 0;

int __cxa_atexit(void (*destructor)(void *), void *arg, void *dso) {
    if (g_cxa_count >= MAX_CXA_ENTRIES)
        return -1;
    g_cxa_entries[g_cxa_count].destructor = destructor;
    g_cxa_entries[g_cxa_count].arg = arg;
    g_cxa_entries[g_cxa_count].dso = dso;
    g_cxa_count++;
    return 0;
}

void __cxa_finalize(void *dso) {
    for (size_t i = g_cxa_count; i > 0; i--) {
        size_t idx = i - 1;
        if (g_cxa_entries[idx].destructor) {
            if (!dso || g_cxa_entries[idx].dso == dso) {
                void (*dtor)(void *) = g_cxa_entries[idx].destructor;
                void *arg = g_cxa_entries[idx].arg;
                g_cxa_entries[idx].destructor = NULL;
                dtor(arg);
            }
        }
    }
}
