#!/usr/bin/env python3
"""
SzpontOS - GNU libstdc++-v3 header installation script
(C) Copyright by Szpont Industries. All rights reserved.

Copies headers out-of-tree from third_party/libstdc++ submodule into sysroot/usr/include/c++
and generates platform-specific configuration headers without modifying the submodule.
"""

import os
import sys
import shutil

def main():
    if len(sys.argv) < 3:
        print("Usage: install_libstdcxx_headers.py <libstdcxx_src_dir> <sysroot_include_cxx_dir>")
        sys.exit(1)

    src_dir = os.path.abspath(sys.argv[1])
    dst_dir = os.path.abspath(sys.argv[2])

    os.makedirs(dst_dir, exist_ok=True)
    bits_dir = os.path.join(dst_dir, "bits")
    ext_dir = os.path.join(dst_dir, "ext")
    backward_dir = os.path.join(dst_dir, "backward")
    os.makedirs(bits_dir, exist_ok=True)
    os.makedirs(ext_dir, exist_ok=True)
    os.makedirs(backward_dir, exist_ok=True)

    # 1. Copy std headers
    std_src = os.path.join(src_dir, "include", "std")
    if os.path.isdir(std_src):
        for f in os.listdir(std_src):
            s = os.path.join(std_src, f)
            if os.path.isfile(s):
                shutil.copy2(s, os.path.join(dst_dir, f))

    # 2. Copy c_global headers
    c_global_src = os.path.join(src_dir, "include", "c_global")
    if os.path.isdir(c_global_src):
        for f in os.listdir(c_global_src):
            s = os.path.join(c_global_src, f)
            if os.path.isfile(s):
                shutil.copy2(s, os.path.join(dst_dir, f))

    # 3. Copy libsupc++ headers
    sup_src = os.path.join(src_dir, "libsupc++")
    if os.path.isdir(sup_src):
        std_sup_headers = ["compare", "cxxabi.h", "exception", "initializer_list", "new", "typeinfo"]
        bits_sup_headers = [
            "atomic_lockfree_defines.h", "cxxabi_forced.h",
            "exception_defines.h", "exception_ptr.h", "hash_bytes.h",
            "nested_exception.h", "exception.h", "cxxabi_init_exception.h"
        ]
        for f in std_sup_headers:
            s = os.path.join(sup_src, f)
            if os.path.isfile(s):
                shutil.copy2(s, os.path.join(dst_dir, f))
        for f in bits_sup_headers:
            s = os.path.join(sup_src, f)
            if os.path.isfile(s):
                shutil.copy2(s, os.path.join(bits_dir, f))

    # 4. Copy bits headers
    bits_src = os.path.join(src_dir, "include", "bits")
    if os.path.isdir(bits_src):
        for f in os.listdir(bits_src):
            s = os.path.join(bits_src, f)
            if os.path.isfile(s):
                shutil.copy2(s, os.path.join(bits_dir, f))

    # 5. Copy ext headers
    ext_src = os.path.join(src_dir, "include", "ext")
    if os.path.isdir(ext_src):
        for f in os.listdir(ext_src):
            s = os.path.join(ext_src, f)
            if os.path.isfile(s):
                shutil.copy2(s, os.path.join(ext_dir, f))

    # 6. Copy backward headers
    bwd_src = os.path.join(src_dir, "include", "backward")
    if os.path.isdir(bwd_src):
        for f in os.listdir(bwd_src):
            s = os.path.join(bwd_src, f)
            if os.path.isfile(s):
                shutil.copy2(s, os.path.join(backward_dir, f))

    # 6b. Copy other standard subdirectories (debug, parallel, pstl, experimental, decimal, tr1, tr2)
    other_subdirs = ["debug", "parallel", "pstl", "experimental", "decimal", "tr1", "tr2"]
    for sub in other_subdirs:
        sub_src = os.path.join(src_dir, "include", sub)
        sub_dst = os.path.join(dst_dir, sub)
        if os.path.isdir(sub_src):
            shutil.copytree(sub_src, sub_dst, dirs_exist_ok=True)

    # 7. Map platform configuration headers
    cfg_mappings = {
        ("config/os/generic/os_defines.h"): "bits/os_defines.h",
        ("config/cpu/generic/cpu_defines.h"): "bits/cpu_defines.h",
        ("config/locale/generic/c_locale.h"): "bits/c++locale.h",
        ("config/os/generic/ctype_base.h"): "bits/ctype_base.h",
        ("config/os/generic/ctype_inline.h"): "bits/ctype_inline.h",
        ("config/allocator/new_allocator_base.h"): "bits/c++allocator.h",
        ("config/io/c_io_stdio.h"): "bits/c++io.h",
        ("config/io/basic_file_stdio.h"): "bits/basic_file.h",
        ("config/os/generic/error_constants.h"): "bits/error_constants.h",
        ("config/cpu/generic/atomic_word.h"): "bits/atomic_word.h",
        ("config/cpu/generic/cxxabi_tweaks.h"): "bits/cxxabi_tweaks.h",
        ("config/locale/generic/time_members.h"): "bits/time_members.h",
        ("config/locale/generic/messages_members.h"): "bits/messages_members.h",
        ("config/cpu/i486/opt/bits/opt_random.h"): "bits/opt_random.h",
        ("config/cpu/i486/opt/ext/opt_random.h"): "ext/opt_random.h",
    }

    for rel_src, rel_dst in cfg_mappings.items():
        src_path = os.path.join(src_dir, rel_src)
        dst_path = os.path.join(dst_dir, rel_dst)
        if os.path.isfile(src_path):
            shutil.copy2(src_path, dst_path)

    with open(os.path.join(bits_dir, "largefile-config.h"), "w") as f:
        f.write("#ifndef _GLIBCXX_LARGEFILE_CONFIG_H\n#define _GLIBCXX_LARGEFILE_CONFIG_H 1\n#define _FILE_OFFSET_BITS 64\n#endif\n")

    # 8. Generate bits/gthr.h and bits/gthr-default.h (pthread integration)
    gthr_content = """/* SzpontOS POSIX gthr wrapper */
#ifndef _GLIBCXX_GTHR_H
#define _GLIBCXX_GTHR_H 1

#include <bits/gthr-default.h>

#endif /* _GLIBCXX_GTHR_H */
"""
    with open(os.path.join(bits_dir, "gthr.h"), "w") as f:
        f.write(gthr_content)

    gthr_default_content = """/* SzpontOS POSIX gthr-default wrapper */
#ifndef _GLIBCXX_GTHR_DEFAULT_H
#define _GLIBCXX_GTHR_DEFAULT_H 1

#define __GTHREADS 1
#define __GTHREADS_CXX0X 1

#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>

typedef pthread_key_t __gthread_key_t;
typedef pthread_once_t __gthread_once_t;
typedef pthread_mutex_t __gthread_mutex_t;
typedef pthread_mutex_t __gthread_recursive_mutex_t;
typedef pthread_cond_t __gthread_cond_t;
typedef pthread_t __gthread_t;
typedef struct timespec __gthread_time_t;

#define __GTHREAD_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER
#define __GTHREAD_ONCE_INIT PTHREAD_ONCE_INIT
#define __GTHREAD_RECURSIVE_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER
#define __GTHREAD_COND_INIT PTHREAD_COND_INITIALIZER
#define __GTHREAD_TIME_INIT {0, 0}

static inline int __gthread_active_p(void) {
    return 1;
}

static inline int __gthread_once(__gthread_once_t *__once, void (*__func)(void)) {
    return pthread_once(__once, __func);
}

static inline int __gthread_key_create(__gthread_key_t *__keyp, void (*__dtor)(void *)) {
    return pthread_key_create(__keyp, __dtor);
}

static inline int __gthread_key_delete(__gthread_key_t __key) {
    return pthread_key_delete(__key);
}

static inline void *__gthread_getspecific(__gthread_key_t __key) {
    return pthread_getspecific(__key);
}

static inline int __gthread_setspecific(__gthread_key_t __key, const void *__ptr) {
    return pthread_setspecific(__key, __ptr);
}

static inline int __gthread_mutex_destroy(__gthread_mutex_t *__mutex) {
    return pthread_mutex_destroy(__mutex);
}

static inline int __gthread_mutex_lock(__gthread_mutex_t *__mutex) {
    return pthread_mutex_lock(__mutex);
}

static inline int __gthread_mutex_trylock(__gthread_mutex_t *__mutex) {
    return pthread_mutex_trylock(__mutex);
}

static inline int __gthread_mutex_unlock(__gthread_mutex_t *__mutex) {
    return pthread_mutex_unlock(__mutex);
}

static inline int __gthread_recursive_mutex_lock(__gthread_recursive_mutex_t *__mutex) {
    return pthread_mutex_lock(__mutex);
}

static inline int __gthread_recursive_mutex_trylock(__gthread_recursive_mutex_t *__mutex) {
    return pthread_mutex_trylock(__mutex);
}

static inline int __gthread_recursive_mutex_unlock(__gthread_recursive_mutex_t *__mutex) {
    return pthread_mutex_unlock(__mutex);
}

static inline int __gthread_recursive_mutex_destroy(__gthread_recursive_mutex_t *__mutex) {
    return pthread_mutex_destroy(__mutex);
}

static inline int __gthread_cond_broadcast(__gthread_cond_t *__cond) {
    return pthread_cond_broadcast(__cond);
}

static inline int __gthread_cond_signal(__gthread_cond_t *__cond) {
    return pthread_cond_signal(__cond);
}

static inline int __gthread_cond_wait(__gthread_cond_t *__cond, __gthread_mutex_t *__mutex) {
    return pthread_cond_wait(__cond, __mutex);
}

static inline int __gthread_cond_timedwait(__gthread_cond_t *__cond, __gthread_mutex_t *__mutex, const __gthread_time_t *__abs_timeout) {
    return pthread_cond_timedwait(__cond, __mutex, __abs_timeout);
}

static inline int __gthread_cond_wait_recursive(__gthread_cond_t *__cond, __gthread_recursive_mutex_t *__mutex) {
    return pthread_cond_wait(__cond, __mutex);
}

static inline int __gthread_cond_destroy(__gthread_cond_t *__cond) {
    return pthread_cond_destroy(__cond);
}

static inline int __gthread_create(__gthread_t *__th, void *(*__func)(void *), void *__args) {
    return pthread_create(__th, NULL, __func, __args);
}

static inline int __gthread_join(__gthread_t __th, void **__res) {
    return pthread_join(__th, __res);
}

static inline int __gthread_detach(__gthread_t __th) {
    return pthread_detach(__th);
}

static inline int __gthread_equal(__gthread_t __t1, __gthread_t __t2) {
    return pthread_equal(__t1, __t2);
}

static inline __gthread_t __gthread_self(void) {
    return pthread_self();
}

static inline int __gthread_yield(void) {
    return sched_yield();
}

#endif /* _GLIBCXX_GTHR_DEFAULT_H */
"""
    with open(os.path.join(bits_dir, "gthr-default.h"), "w") as f:
        f.write(gthr_default_content)

    # 9. Generate bits/c++config.h
    raw_cfg = os.path.join(src_dir, "include", "bits", "c++config")
    with open(raw_cfg, "r") as f:
        content = f.read()

    # Apply substitutions
    content = content.replace("define __GLIBCXX__", "define __GLIBCXX__ 20240901")
    content = content.replace("define _GLIBCXX_RELEASE", "define _GLIBCXX_RELEASE 14")
    content = content.replace("define _GLIBCXX_INLINE_VERSION", "define _GLIBCXX_INLINE_VERSION 0")
    content = content.replace("define _GLIBCXX_HAVE_ATTRIBUTE_VISIBILITY", "define _GLIBCXX_HAVE_ATTRIBUTE_VISIBILITY 1")
    content = content.replace("define _GLIBCXX_USE_DUAL_ABI", "define _GLIBCXX_USE_DUAL_ABI 1")
    content = content.replace("define _GLIBCXX_USE_CXX11_ABI", "define _GLIBCXX_USE_CXX11_ABI 1")
    content = content.replace("define _GLIBCXX_USE_ALLOCATOR_NEW", "define _GLIBCXX_USE_ALLOCATOR_NEW 1")

    # Add platform features at the end
    target_config = """

// ============================================================================
// SzpontOS Target Configuration Definitions
// ============================================================================
#ifndef _GLIBCXX_HOSTED
#define _GLIBCXX_HOSTED 1
#endif

#define _GLIBCXX_HAVE_SYS_TYPES_H 1
#define _GLIBCXX_HAVE_STDIO_H 1
#define _GLIBCXX_HAVE_STDLIB_H 1
#define _GLIBCXX_HAVE_STRING_H 1
#define _GLIBCXX_HAVE_INTTYPES_H 1
#define _GLIBCXX_HAVE_STDINT_H 1
#define _GLIBCXX_HAVE_STRINGS_H 1
#define _GLIBCXX_HAVE_UNISTD_H 1
#define _GLIBCXX_HAVE_WCHAR_H 1
#define _GLIBCXX_HAVE_WCTYPE_H 1
#define _GLIBCXX_HAVE_PTHREAD_H 1
#define _GLIBCXX_HAVE_FCNTL_H 1
#define _GLIBCXX_HAVE_SYS_STAT_H 1
#define _GLIBCXX_HAVE_SYS_TIME_H 1
#define _GLIBCXX_HAVE_POLL_H 1
#define _GLIBCXX_HAVE_SYS_IOCTL_H 1

#define _GLIBCXX_USE_WCHAR_T 1
#define _GLIBCXX_HAVE_MBSTATE_T 1
#define _GLIBCXX_USE_C99 1
#define _GLIBCXX_USE_C99_MATH 1
#define _GLIBCXX_USE_C99_MATH_TR1 1
#define _GLIBCXX_USE_C99_STDIO 1
#define _GLIBCXX_USE_C99_STDLIB 1
#define _GLIBCXX_USE_C99_WCHAR 1
#define _GLIBCXX_HAVE_ISWBLANK 1

#define _GLIBCXX_HAVE__EXIT 1
#define _GLIBCXX_HAVE_STRTOF 1
#define _GLIBCXX_HAVE_STRTOLD 1
#define _GLIBCXX_HAVE_SNPRINTF 1
#define _GLIBCXX_HAVE_VSNPRINTF 1
#define _GLIBCXX_HAVE_VFSCANF 1
#define _GLIBCXX_HAVE_VSSCANF 1
#define _GLIBCXX_HAVE_VSCANF 1

#define _GLIBCXX_HAVE_WCSTOF 1
#define _GLIBCXX_HAVE_WCSTOLD 1
#define _GLIBCXX_HAVE_WCSTOLL 1
#define _GLIBCXX_HAVE_WCSTOULL 1

#define _GLIBCXX_STDIO_SEEK_SET 0
#define _GLIBCXX_STDIO_SEEK_CUR 1
#define _GLIBCXX_STDIO_SEEK_END 2
#define _GLIBCXX_STDIO_EOF (-1)
#define _GLIBCXX_EXTERN_TEMPLATE 1
#define _GLIBCXX_USE_STD_SPEC_FUNCS 0

#define _GLIBCXX_ATOMIC_BUILTINS 1
#define _GLIBCXX_ATOMIC_BUILTINS_1 1
#define _GLIBCXX_ATOMIC_BUILTINS_2 1
#define _GLIBCXX_ATOMIC_BUILTINS_4 1
#define _GLIBCXX_ATOMIC_BUILTINS_8 1

#define _GLIBCXX_USE_DECIMAL_FLOAT 0

#include <bits/os_defines.h>
#include <bits/cpu_defines.h>

#endif /* _GLIBCXX_CXX_CONFIG_H */
"""
    # Remove the trailing '#endif' if it exists to replace it with target_config
    if content.strip().endswith("#endif"):
        content = content.strip()[:-6] + target_config
    else:
        content = content + target_config

    with open(os.path.join(bits_dir, "c++config.h"), "w") as f:
        f.write(content)

    print(f"[OK] Installed libstdc++ headers to {dst_dir}")

if __name__ == "__main__":
    main()
