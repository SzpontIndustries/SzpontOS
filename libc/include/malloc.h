#ifndef _MALLOC_H
#define _MALLOC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stddef.h>

size_t malloc_usable_size(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* _MALLOC_H */
