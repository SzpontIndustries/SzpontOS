#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

extern int64_t __syscall3(int64_t num, int64_t a1, int64_t a2, int64_t a3);
#define SYS_getdents 78

DIR *opendir(const char *name) {
    int fd = open(name, O_RDONLY);
    if (fd < 0)
        return NULL;

    DIR *dir = (DIR *)malloc(sizeof(DIR));
    if (!dir) {
        close(fd);
        return NULL;
    }
    dir->fd = fd;
    memset(&dir->current, 0, sizeof(struct dirent));
    return dir;
}

DIR *fdopendir(int fd) {
    if (fd < 0)
        return NULL;
    DIR *dir = (DIR *)malloc(sizeof(DIR));
    if (!dir)
        return NULL;
    dir->fd = fd;
    memset(&dir->current, 0, sizeof(struct dirent));
    return dir;
}

int dirfd(DIR *dirp) {
    if (!dirp)
        return -1;
    return dirp->fd;
}

struct dirent *readdir(DIR *dirp) {
    if (!dirp || dirp->fd < 0)
        return NULL;

    ssize_t bytes = __syscall3(SYS_getdents, dirp->fd, (int64_t)&dirp->current, sizeof(struct dirent));
    if (bytes <= 0) {
        return NULL;
    }
    dirp->current.d_namlen = (uint16_t)strlen(dirp->current.d_name);
    return &dirp->current;
}

void rewinddir(DIR *dirp) {
    if (!dirp || dirp->fd < 0)
        return;
    lseek(dirp->fd, 0, SEEK_SET);
}

int closedir(DIR *dirp) {
    if (!dirp)
        return -1;
    int ret = close(dirp->fd);
    free(dirp);
    return ret;
}

int alphasort(const struct dirent **a, const struct dirent **b) {
    if (!a || !*a || !b || !*b)
        return 0;
    return strcmp((*a)->d_name, (*b)->d_name);
}

int scandir(const char *dirp, struct dirent ***namelist,
            int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **, const struct dirent **)) {
    if (!dirp || !namelist)
        return -1;

    DIR *d = opendir(dirp);
    if (!d)
        return -1;

    size_t capacity = 16;
    size_t count = 0;
    struct dirent **list = (struct dirent **)malloc(capacity * sizeof(struct dirent *));
    if (!list) {
        closedir(d);
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (filter && !filter(entry))
            continue;

        if (count >= capacity) {
            size_t new_cap = capacity * 2;
            struct dirent **new_list = (struct dirent **)realloc(list, new_cap * sizeof(struct dirent *));
            if (!new_list) {
                for (size_t i = 0; i < count; i++)
                    free(list[i]);
                free(list);
                closedir(d);
                return -1;
            }
            list = new_list;
            capacity = new_cap;
        }

        struct dirent *copy = (struct dirent *)malloc(sizeof(struct dirent));
        if (!copy) {
            for (size_t i = 0; i < count; i++)
                free(list[i]);
            free(list);
            closedir(d);
            return -1;
        }
        memcpy(copy, entry, sizeof(struct dirent));
        list[count++] = copy;
    }

    closedir(d);

    if (compar && count > 1) {
        qsort(list, count, sizeof(struct dirent *), (int (*)(const void *, const void *))compar);
    }

    *namelist = list;
    return (int)count;
}

