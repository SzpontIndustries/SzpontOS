#ifndef SZPONTOS_FS_VFS_H
#define SZPONTOS_FS_VFS_H

#include <kernel/types.h>

#define VFS_TYPE_FILE 1
#define VFS_TYPE_DIRECTORY 2
#define VFS_TYPE_CHARDEVICE 3
#define VFS_TYPE_BLOCKDEVICE 4
#define VFS_TYPE_PIPE 5
#define VFS_TYPE_SYMLINK 6
#define VFS_TYPE_SOCKET 7

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR 0x0002
#define O_CREAT 0x0040
#define O_EXCL 0x0080
#define O_NOCTTY 0x0100
#define O_TRUNC 0x0200
#define O_APPEND 0x0400

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

struct vfs_node;

typedef struct vfs_dirent {
    char name[128];
    uint32_t inode;
    uint32_t type;
} vfs_dirent_t;

#define VFS_READ 4
#define VFS_WRITE 2
#define VFS_EXEC 1

#ifndef S_IFMT
#define S_IFMT   0170000
#define S_IFIFO  0010000
#define S_IFCHR  0020000
#define S_IFDIR  0040000
#define S_IFBLK  0060000
#define S_IFREG  0100000
#define S_IFLNK  0120000
#define S_IFSOCK 0140000

#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#endif

typedef struct vfs_ops {
    ssize_t (*read)(struct vfs_node *node, off_t offset, size_t size, void *buffer);
    ssize_t (*write)(struct vfs_node *node, off_t offset, size_t size, const void *buffer);
    int (*open)(struct vfs_node *node, uint32_t flags);
    int (*close)(struct vfs_node *node);
    struct vfs_dirent *(*readdir)(struct vfs_node *node, uint32_t index);
    struct vfs_node *(*finddir)(struct vfs_node *node, const char *name);
    int (*create)(struct vfs_node *parent, const char *name, mode_t mode);
    int (*mkdir)(struct vfs_node *parent, const char *name, mode_t mode);
    int (*chmod)(struct vfs_node *node, mode_t mode);
    int (*chown)(struct vfs_node *node, uid_t uid, gid_t gid);
    int (*unlink)(struct vfs_node *parent, const char *name);
    int (*ioctl)(struct vfs_node *node, uint64_t request, uintptr_t arg);
    int (*rename)(struct vfs_node *old_parent, const char *old_name, struct vfs_node *new_parent, const char *new_name);
    int (*rmdir)(struct vfs_node *parent, const char *name);
    int (*truncate)(struct vfs_node *node, off_t length);
    int (*symlink)(struct vfs_node *parent, const char *name, const char *target);
    ssize_t (*readlink)(struct vfs_node *node, char *buf, size_t bufsiz);
    int (*link)(struct vfs_node *parent, struct vfs_node *source, const char *new_name);
    int (*access)(struct vfs_node *node, int mode);
    int (*mmap)(struct vfs_node *node, void *addr, size_t length, int prot, int flags, off_t offset, void **out_vaddr);
    int (*mknod)(struct vfs_node *parent, const char *name, mode_t mode, dev_t dev);
    int (*fsync)(struct vfs_node *node);
} vfs_ops_t;

typedef struct vfs_node {
    char name[128];
    uint32_t flags; /* VFS_TYPE_* */
    uint32_t inode;
    size_t length;
    uint32_t uid;
    uint32_t gid;
    uint32_t permissions;
    uint32_t rdev;        /* Device ID (major/minor) for char/block devices */
    vfs_ops_t *ops;
    void *device_data;    /* Driver private data */
    struct vfs_node *ptr; /* Mount point or symlink redirection */
    /* Advisory lock state (flock) */
    int lock_type;        /* 0 = unlocked, 1 = LOCK_SH, 2 = LOCK_EX */
    int lock_count;       /* Number of shared lock holders */
    pid_t lock_owner;     /* PID holding exclusive lock */
} vfs_node_t;

typedef struct file_descriptor {
    vfs_node_t *node;
    off_t offset;
    uint32_t flags;
    uint32_t refcount;
    /* memfd(2) sealing state. is_memfd marks files created via memfd_create
     * so fcntl(F_ADD_SEALS/F_GET_SEALS) can validate the target. */
    bool is_memfd;
    bool memfd_allow_sealing;
    uint32_t memfd_seals; /* F_SEAL_* bitmask */
} file_descriptor_t;

typedef struct vfs_mount_info {
    char path[128];
    char name[128];
    uint32_t flags;
} vfs_mount_info_t;

void vfs_init(void);
int vfs_mount(const char *path, vfs_node_t *node);
vfs_node_t *vfs_lookup(const char *path);
vfs_node_t *vfs_lookup_nofollow(const char *path);
int vfs_unlink(const char *path);
int vfs_mkdir(const char *path, mode_t mode);
int vfs_mknod(const char *path, mode_t mode, dev_t dev);
int vfs_rmdir(const char *path);
int vfs_rename(const char *oldpath, const char *newpath);
int vfs_truncate(const char *path, off_t length);
int vfs_symlink(const char *target, const char *linkpath);
int vfs_link(const char *oldpath, const char *newpath);
ssize_t vfs_readlink(const char *path, char *buf, size_t bufsiz);
int vfs_access(const char *path, int mode);
int vfs_check_permission(vfs_node_t *node, int mask);
void vfs_normalize_path(const char *src, char *dst, size_t dst_size);
int vfs_resolve_path(const char *src, char *dst, size_t dst_size);
size_t vfs_get_mount_list(vfs_mount_info_t *buf, size_t max_count);

/* High level POSIX-like VFS calls */
int vfs_open(const char *path, int flags, mode_t mode);
int vfs_close(int fd);
ssize_t vfs_read(int fd, void *buf, size_t count);
ssize_t vfs_write(int fd, const void *buf, size_t count);
off_t vfs_lseek(int fd, off_t offset, int whence);
int vfs_chmod(const char *path, mode_t mode);
int vfs_chown(const char *path, uid_t uid, gid_t gid);
/* File descriptor lifecycle */
void fd_release(file_descriptor_t *f);
short vfs_poll_node(file_descriptor_t *fdesc, short events);

/* Standard poll(2) / epoll event mask flags */
#ifndef POLLIN
#define POLLIN      0x0001
#define POLLPRI     0x0002
#define POLLOUT     0x0004
#define POLLERR     0x0008
#define POLLHUP     0x0010
#define POLLNVAL    0x0020
#define POLLRDNORM  0x0040
#define POLLRDBAND  0x0080
#define POLLWRNORM  0x0100
#define POLLWRBAND  0x0200
#endif

#endif /* SZPONTOS_FS_VFS_H */
