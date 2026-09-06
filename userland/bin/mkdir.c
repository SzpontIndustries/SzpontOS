#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static int make_path(char *path, mode_t mode) {
    char *p = path;
    if (*p == '/')
        p++;

    while (*p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(path, mode);
            *p = '/';
        }
        p++;
    }
    return mkdir(path, mode);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: mkdir [-p] <directory...>\n");
        return 1;
    }

    int parents = 0;
    int first_arg = 1;

    if (strcmp(argv[1], "-p") == 0 || strcmp(argv[1], "--parents") == 0) {
        parents = 1;
        first_arg = 2;
    }

    if (first_arg >= argc) {
        printf("mkdir: missing operand\n");
        return 1;
    }

    int status = 0;
    for (int i = first_arg; i < argc; i++) {
        int ret;
        if (parents) {
            char buf[256];
            strncpy(buf, argv[i], sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            ret = make_path(buf, 0755);
        } else {
            ret = mkdir(argv[i], 0755);
        }

        if (ret != 0) {
            struct stat st;
            if (parents && stat(argv[i], &st) == 0 && S_ISDIR(st.st_mode)) {
                /* Directory already exists; -p ignores it */
            } else {
                printf("mkdir: cannot create directory '%s'\n", argv[i]);
                status = 1;
            }
        }
    }

    return status;
}
