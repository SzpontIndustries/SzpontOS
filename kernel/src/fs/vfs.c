#include <fs/vfs.h>
#include <fs/pipe.h>
#include <sched/process.h>
#include <sched/sched.h>
#include <mm/heap.h>
#include <kernel/string.h>
#include <kernel/kprint.h>
#include <kernel/spinlock.h>

#define MAX_MOUNTS 32

typedef struct vfs_mount {
    char path[128];
    vfs_node_t *node;
} vfs_mount_t;

static vfs_node_t *g_vfs_root = NULL;
static vfs_mount_t g_mounts[MAX_MOUNTS];
static size_t g_mount_count = 0;
static spinlock_t g_vfs_lock = SPINLOCK_INIT;

void vfs_init(void) {
    spinlock_init(&g_vfs_lock);
    memset(g_mounts, 0, sizeof(g_mounts));
    g_mount_count = 0;

    g_vfs_root = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    strcpy(g_vfs_root->name, "/");
    g_vfs_root->flags = VFS_TYPE_DIRECTORY;
    g_vfs_root->permissions = 0755;
    g_vfs_root->uid = 0;
    g_vfs_root->gid = 0;

    klog_info("VFS initialized with root node '/'");
}

int vfs_mount(const char *path, vfs_node_t *node) {
    if (!path || !node)
        return -1;

    spinlock_acquire(&g_vfs_lock);
    if (g_mount_count >= MAX_MOUNTS) {
        spinlock_release(&g_vfs_lock);
        return -1;
    }

    if (strcmp(path, "/") == 0) {
        g_vfs_root = node;
        klog_info("VFS: Mounted root filesystem '/' at %s", node->name);
        spinlock_release(&g_vfs_lock);
        return 0;
    }

    /* Auto-create mount point directory in parent filesystem if not existing */
    if (g_vfs_root && g_vfs_root->ops && g_vfs_root->ops->mkdir) {
        const char *mount_name = (path[0] == '/') ? path + 1 : path;
        if (mount_name[0] && strchr(mount_name, '/') == NULL) {
            g_vfs_root->ops->mkdir(g_vfs_root, mount_name, 0755);
        }
    }

    strncpy(g_mounts[g_mount_count].path, path, 127);
    g_mounts[g_mount_count].node = node;
    g_mount_count++;

    klog_info("VFS: Mounted filesystem at '%s'", path);
    spinlock_release(&g_vfs_lock);
    return 0;
}

size_t vfs_get_mount_list(vfs_mount_info_t *buf, size_t max_count) {
    if (!buf || max_count == 0)
        return 0;
    spinlock_acquire(&g_vfs_lock);
    size_t count = 0;

    if (g_vfs_root && count < max_count) {
        strncpy(buf[count].path, "/", 127);
        strncpy(buf[count].name,
                (g_vfs_root->name[0] && strcmp(g_vfs_root->name, "/") != 0) ? g_vfs_root->name : "rootfs", 127);
        buf[count].flags = g_vfs_root->flags;
        count++;
    }

    for (size_t i = 0; i < g_mount_count && count < max_count; i++) {
        strncpy(buf[count].path, g_mounts[i].path, 127);
        if (g_mounts[i].node) {
            strncpy(buf[count].name, g_mounts[i].node->name, 127);
            buf[count].flags = g_mounts[i].node->flags;
        } else {
            strcpy(buf[count].name, "unknown");
        }
        count++;
    }

    spinlock_release(&g_vfs_lock);
    return count;
}

void vfs_normalize_path(const char *src, char *dst, size_t dst_size) {
    if (!src || !dst || dst_size == 0)
        return;

    char temp[512];
    strncpy(temp, src, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *tokens[64];
    int token_count = 0;

    char *p = temp;
    while (*p == '/')
        p++;

    while (*p) {
        char *slash = strchr(p, '/');
        if (slash)
            *slash = '\0';

        if (strcmp(p, ".") == 0 || *p == '\0') {
            /* ignore current dir reference */
        } else if (strcmp(p, "..") == 0) {
            if (token_count > 0) {
                token_count--;
            }
        } else {
            if (token_count < 64) {
                tokens[token_count++] = p;
            }
        }

        if (slash)
            p = slash + 1;
        else
            break;
    }

    if (token_count == 0) {
        strncpy(dst, "/", dst_size - 1);
        dst[dst_size - 1] = '\0';
        return;
    }

    dst[0] = '\0';
    for (int i = 0; i < token_count; i++) {
        size_t cur_len = strlen(dst);
        ksnprintf(dst + cur_len, dst_size - cur_len, "/%s", tokens[i]);
    }
}

int vfs_resolve_path(const char *src, char *dst, size_t dst_size) {
    if (!src || !dst || dst_size == 0)
        return -1;

    char combined[512];
    process_t *proc = sched_get_current_process();

    if (src[0] == '/') {
        strncpy(combined, src, sizeof(combined) - 1);
    } else {
        const char *cwd = (proc && proc->cwd[0]) ? proc->cwd : "/";
        if (strcmp(cwd, "/") == 0) {
            ksnprintf(combined, sizeof(combined), "/%s", src);
        } else {
            ksnprintf(combined, sizeof(combined), "%s/%s", cwd, src);
        }
    }
    combined[sizeof(combined) - 1] = '\0';

    vfs_normalize_path(combined, dst, dst_size);
    return 0;
}

static vfs_node_t *vfs_lookup_internal(const char *path, bool follow_symlinks, int depth) {
    if (!path || !g_vfs_root || depth > 16)
        return NULL;

    char norm_path[256];
    vfs_resolve_path(path, norm_path, sizeof(norm_path));
    if (strcmp(norm_path, "/") == 0)
        return g_vfs_root;

    /* Check mount point prefix matching */
    vfs_node_t *current = g_vfs_root;
    const char *subpath = norm_path;
    char parent_dir[256] = "/";

    for (size_t i = 0; i < g_mount_count; i++) {
        size_t mlen = strlen(g_mounts[i].path);
        if (strncmp(norm_path, g_mounts[i].path, mlen) == 0) {
            if (norm_path[mlen] == '/' || norm_path[mlen] == '\0') {
                current = g_mounts[i].node;
                strncpy(parent_dir, g_mounts[i].path, sizeof(parent_dir) - 1);
                parent_dir[sizeof(parent_dir) - 1] = '\0';
                subpath = norm_path + mlen;
                if (*subpath == '/')
                    subpath++;
                if (*subpath == '\0')
                    return current;
                break;
            }
        }
    }

    /* Tokenize path and traverse down */
    char temp[256];
    strncpy(temp, subpath, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *token = temp;
    while (*token == '/')
        token++;

    char *next_slash;
    while (token && *token) {
        next_slash = strchr(token, '/');
        if (next_slash) {
            *next_slash = '\0';
        }

        if (strcmp(token, ".") == 0) {
            /* Current directory */
        } else if (strcmp(token, "..") == 0) {
            /* Handled in normalization */
        } else {
            if (!current->ops || !current->ops->finddir) {
                return NULL;
            }
            vfs_node_t *next = current->ops->finddir(current, token);
            if (!next) {
                return NULL;
            }
            if (next->ptr) {
                next = next->ptr;
            }

            /* Handle symlinks */
            if (next->flags == VFS_TYPE_SYMLINK) {
                bool is_last = (next_slash == NULL || *(next_slash + 1) == '\0');
                if (follow_symlinks || !is_last) {
                    char target[256] = {0};
                    if (next->ops && next->ops->readlink) {
                        next->ops->readlink(next, target, sizeof(target) - 1);
                    } else if (next->device_data) {
                        strncpy(target, (const char *)next->device_data, sizeof(target) - 1);
                    }

                    if (target[0]) {
                        char full_target[512];
                        if (target[0] == '/') {
                            strncpy(full_target, target, sizeof(full_target) - 1);
                        } else {
                            /* Resolve relative to parent directory */
                            if (strcmp(parent_dir, "/") == 0) {
                                ksnprintf(full_target, sizeof(full_target), "/%s", target);
                            } else {
                                ksnprintf(full_target, sizeof(full_target), "%s/%s", parent_dir, target);
                            }
                        }
                        full_target[sizeof(full_target) - 1] = '\0';

                        if (!is_last && next_slash) {
                            size_t tlen = strlen(full_target);
                            ksnprintf(full_target + tlen, sizeof(full_target) - tlen, "/%s", next_slash + 1);
                        }

                        return vfs_lookup_internal(full_target, follow_symlinks, depth + 1);
                    }
                }
            }

            current = next;
            if (strcmp(parent_dir, "/") == 0) {
                ksnprintf(parent_dir, sizeof(parent_dir), "/%s", token);
            } else {
                ksnprintf(parent_dir, sizeof(parent_dir), "%s/%s", parent_dir, token);
            }
        }

        if (next_slash) {
            token = next_slash + 1;
        } else {
            break;
        }
    }

    return current;
}

vfs_node_t *vfs_lookup(const char *path) {
    return vfs_lookup_internal(path, true, 0);
}

vfs_node_t *vfs_lookup_nofollow(const char *path) {
    return vfs_lookup_internal(path, false, 0);
}

int vfs_check_permission(vfs_node_t *node, int mask) {
    if (!node)
        return -1;

    process_t *curr = sched_get_current_process();
    if (!curr)
        return 0; /* Kernel has full access */

    /* Superuser (root) bypasses normal checks */
    if (curr->euid == 0) {
        /* If execute is requested on a file, at least one execute bit must be set */
        if ((mask & VFS_EXEC) && (node->flags != VFS_TYPE_DIRECTORY)) {
            if ((node->permissions & 0111) == 0) {
                return -1; /* Cannot execute file with no exec bits */
            }
        }
        return 0;
    }

    /* Owner check */
    if (curr->euid == node->uid) {
        uint32_t owner_perm = (node->permissions >> 6) & 7;
        if ((owner_perm & mask) == (uint32_t)mask) {
            return 0;
        }
        return -1;
    }

    /* Group check (primary and supplementary groups) */
    bool group_match = (curr->egid == node->gid);
    if (!group_match) {
        for (int i = 0; i < curr->ngroups; i++) {
            if (curr->groups[i] == node->gid) {
                group_match = true;
                break;
            }
        }
    }
    if (group_match) {
        uint32_t group_perm = (node->permissions >> 3) & 7;
        if ((group_perm & mask) == (uint32_t)mask) {
            return 0;
        }
        return -1;
    }

    /* Others check */
    uint32_t other_perm = node->permissions & 7;
    if ((other_perm & mask) == (uint32_t)mask) {
        return 0;
    }

    return -1;
}

static int vfs_split_parent(const char *path, char *parent_out, size_t parent_sz, char *name_out, size_t name_sz) {
    if (!path || !parent_out || !name_out)
        return -1;

    char resolved[256];
    if (vfs_resolve_path(path, resolved, sizeof(resolved)) != 0)
        return -1;

    /* Strip trailing slashes */
    size_t plen = strlen(resolved);
    while (plen > 1 && resolved[plen - 1] == '/') {
        resolved[--plen] = '\0';
    }

    char *last_slash = strrchr(resolved, '/');
    if (!last_slash) {
        strncpy(name_out, resolved, name_sz - 1);
        name_out[name_sz - 1] = '\0';
        strncpy(parent_out, "/", parent_sz - 1);
        parent_out[parent_sz - 1] = '\0';
    } else if (last_slash == resolved) {
        strncpy(name_out, last_slash + 1, name_sz - 1);
        name_out[name_sz - 1] = '\0';
        strncpy(parent_out, "/", parent_sz - 1);
        parent_out[parent_sz - 1] = '\0';
    } else {
        strncpy(name_out, last_slash + 1, name_sz - 1);
        name_out[name_sz - 1] = '\0';
        *last_slash = '\0';
        strncpy(parent_out, resolved, parent_sz - 1);
        parent_out[parent_sz - 1] = '\0';
    }

    return 0;
}

int vfs_chmod(const char *path, mode_t mode) {
    if (!path)
        return -22;

    char full_path[256];
    if (vfs_resolve_path(path, full_path, sizeof(full_path)) != 0)
        return -2;

    vfs_node_t *node = vfs_lookup(full_path);
    if (!node)
        return -2;

    process_t *curr = sched_get_current_process();
    if (curr && curr->euid != 0 && curr->euid != node->uid) {
        return -1; /* EPERM */
    }

    node->permissions = (mode & 07777);
    if (node->ops && node->ops->chmod) {
        return node->ops->chmod(node, mode);
    }
    return 0;
}

int vfs_chown(const char *path, uid_t uid, gid_t gid) {
    if (!path)
        return -22;

    char full_path[256];
    if (vfs_resolve_path(path, full_path, sizeof(full_path)) != 0)
        return -2;

    vfs_node_t *node = vfs_lookup(full_path);
    if (!node)
        return -2;

    process_t *curr = sched_get_current_process();
    if (curr && curr->euid != 0) {
        return -1; /* EPERM: Only root can change owner */
    }

    if (uid != (uid_t)-1) {
        node->uid = uid;
    }
    if (gid != (gid_t)-1) {
        node->gid = gid;
    }

    if (node->ops && node->ops->chown) {
        return node->ops->chown(node, uid, gid);
    }
    return 0;
}

int vfs_mkdir(const char *path, mode_t mode) {
    if (!path || !*path)
        return -22;

    char full_path[256];
    if (vfs_resolve_path(path, full_path, sizeof(full_path)) != 0)
        return -2;

    if (vfs_lookup(full_path) != NULL)
        return -17; /* EEXIST */

    char parent_path[256];
    char dir_name[128];
    if (vfs_split_parent(full_path, parent_path, sizeof(parent_path), dir_name, sizeof(dir_name)) != 0)
        return -2;

    vfs_node_t *parent = vfs_lookup(parent_path);
    if (!parent)
        return -2; /* ENOENT */
    if (parent->flags != VFS_TYPE_DIRECTORY)
        return -20; /* ENOTDIR */

    if (vfs_check_permission(parent, VFS_WRITE | VFS_EXEC) != 0) {
        return -13; /* EACCES: Permission denied in parent directory */
    }

    if (parent->ops && parent->ops->mkdir) {
        return parent->ops->mkdir(parent, dir_name, mode);
    }

    return -1;
}

int vfs_unlink(const char *path) {
    if (!path || !*path)
        return -22;

    char full_path[256];
    if (vfs_resolve_path(path, full_path, sizeof(full_path)) != 0)
        return -2;

    char parent_path[256];
    char entry_name[128];
    if (vfs_split_parent(full_path, parent_path, sizeof(parent_path), entry_name, sizeof(entry_name)) != 0)
        return -2;

    vfs_node_t *parent = vfs_lookup(parent_path);
    if (!parent)
        return -2;
    if (parent->flags != VFS_TYPE_DIRECTORY)
        return -20;

    if (vfs_check_permission(parent, VFS_WRITE | VFS_EXEC) != 0) {
        return -13; /* EACCES: Permission denied */
    }

    if (parent->ops && parent->ops->unlink) {
        return parent->ops->unlink(parent, entry_name);
    }

    return -1;
}

int vfs_rmdir(const char *path) {
    if (!path || !*path)
        return -22;

    char full_path[256];
    if (vfs_resolve_path(path, full_path, sizeof(full_path)) != 0)
        return -2;

    char parent_path[256];
    char dir_name[128];
    if (vfs_split_parent(full_path, parent_path, sizeof(parent_path), dir_name, sizeof(dir_name)) != 0)
        return -2;

    vfs_node_t *parent = vfs_lookup(parent_path);
    if (!parent)
        return -2;
    if (parent->flags != VFS_TYPE_DIRECTORY)
        return -20;

    if (vfs_check_permission(parent, VFS_WRITE | VFS_EXEC) != 0) {
        return -13; /* EACCES: Permission denied */
    }

    if (parent->ops && parent->ops->rmdir) {
        return parent->ops->rmdir(parent, dir_name);
    } else if (parent->ops && parent->ops->unlink) {
        return parent->ops->unlink(parent, dir_name);
    }

    return -1;
}

int vfs_rename(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath)
        return -22;

    char full_old[256], full_new[256];
    if (vfs_resolve_path(oldpath, full_old, sizeof(full_old)) != 0)
        return -2;
    if (vfs_resolve_path(newpath, full_new, sizeof(full_new)) != 0)
        return -2;

    char old_parent_path[256], old_name[128];
    char new_parent_path[256], new_name[128];

    if (vfs_split_parent(full_old, old_parent_path, sizeof(old_parent_path), old_name, sizeof(old_name)) != 0)
        return -2;
    if (vfs_split_parent(full_new, new_parent_path, sizeof(new_parent_path), new_name, sizeof(new_name)) != 0)
        return -2;

    vfs_node_t *old_parent = vfs_lookup(old_parent_path);
    vfs_node_t *new_parent = vfs_lookup(new_parent_path);

    if (!old_parent || !new_parent)
        return -2;

    if (vfs_check_permission(old_parent, VFS_WRITE | VFS_EXEC) != 0 ||
        vfs_check_permission(new_parent, VFS_WRITE | VFS_EXEC) != 0) {
        return -13; /* EACCES */
    }

    if (old_parent->ops && old_parent->ops->rename) {
        return old_parent->ops->rename(old_parent, old_name, new_parent, new_name);
    }

    return -1;
}

int vfs_truncate(const char *path, off_t length) {
    if (!path || length < 0)
        return -22;

    char full_path[256];
    if (vfs_resolve_path(path, full_path, sizeof(full_path)) != 0)
        return -2;

    vfs_node_t *node = vfs_lookup(full_path);
    if (!node)
        return -2;
    if (node->flags == VFS_TYPE_DIRECTORY)
        return -21; /* EISDIR */

    if (vfs_check_permission(node, VFS_WRITE) != 0)
        return -13;

    if (node->ops && node->ops->truncate) {
        return node->ops->truncate(node, length);
    }

    node->length = (size_t)length;
    return 0;
}

int vfs_symlink(const char *target, const char *linkpath) {
    if (!target || !linkpath)
        return -22;

    char full_link[256];
    if (vfs_resolve_path(linkpath, full_link, sizeof(full_link)) != 0)
        return -2;

    char parent_path[256];
    char link_name[128];
    if (vfs_split_parent(full_link, parent_path, sizeof(parent_path), link_name, sizeof(link_name)) != 0)
        return -2;

    vfs_node_t *parent = vfs_lookup(parent_path);
    if (!parent)
        return -2;
    if (parent->flags != VFS_TYPE_DIRECTORY)
        return -20;

    if (vfs_check_permission(parent, VFS_WRITE | VFS_EXEC) != 0)
        return -13;

    if (parent->ops && parent->ops->symlink) {
        return parent->ops->symlink(parent, link_name, target);
    }

    return -1;
}

int vfs_link(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath)
        return -22; /* EINVAL */

    char full_old[256];
    if (vfs_resolve_path(oldpath, full_old, sizeof(full_old)) != 0)
        return -2; /* ENOENT */

    vfs_node_t *old_node = vfs_lookup(full_old);
    if (!old_node)
        return -2; /* ENOENT */

    if (old_node->flags == VFS_TYPE_DIRECTORY)
        return -1; /* EPERM */

    char full_new[256];
    if (vfs_resolve_path(newpath, full_new, sizeof(full_new)) != 0)
        return -2; /* ENOENT */

    char parent_path[256];
    char link_name[128];
    if (vfs_split_parent(full_new, parent_path, sizeof(parent_path), link_name, sizeof(link_name)) != 0)
        return -2;

    vfs_node_t *parent = vfs_lookup(parent_path);
    if (!parent)
        return -2;
    if (parent->flags != VFS_TYPE_DIRECTORY)
        return -20; /* ENOTDIR */

    if (vfs_check_permission(parent, VFS_WRITE | VFS_EXEC) != 0)
        return -13; /* EACCES */

    if (parent->ops && parent->ops->link) {
        return parent->ops->link(parent, old_node, link_name);
    }

    return -1;
}


ssize_t vfs_readlink(const char *path, char *buf, size_t bufsiz) {
    if (!path || !buf || bufsiz == 0)
        return -22;

    char full_path[256];
    if (vfs_resolve_path(path, full_path, sizeof(full_path)) != 0)
        return -2;

    vfs_node_t *node = vfs_lookup_nofollow(full_path);
    if (!node)
        return -2;
    if (node->flags != VFS_TYPE_SYMLINK)
        return -22;

    if (node->ops && node->ops->readlink) {
        return node->ops->readlink(node, buf, bufsiz);
    } else if (node->device_data) {
        const char *tgt = (const char *)node->device_data;
        size_t len = strlen(tgt);
        if (len > bufsiz)
            len = bufsiz;
        memcpy(buf, tgt, len);
        return (ssize_t)len;
    }

    return -1;
}

int vfs_access(const char *path, int mode) {
    if (!path)
        return -22;

    char full_path[256];
    if (vfs_resolve_path(path, full_path, sizeof(full_path)) != 0)
        return -2;

    vfs_node_t *node = vfs_lookup(full_path);
    if (!node)
        return -2;

    if (mode == 0) /* F_OK */
        return 0;

    int mask = 0;
    if (mode & 4) /* R_OK */
        mask |= VFS_READ;
    if (mode & 2) /* W_OK */
        mask |= VFS_WRITE;
    if (mode & 1) /* X_OK */
        mask |= VFS_EXEC;

    if (vfs_check_permission(node, mask) != 0)
        return -13;

    return 0;
}

void fd_release(file_descriptor_t *f) {
    if (!f)
        return;
    f->refcount--;
    if (f->refcount == 0) {
        if (f->node) {
            if ((f->node->flags == VFS_TYPE_PIPE) && f->node->device_data) {
                pipe_chan_t *p = (pipe_chan_t *)f->node->device_data;
                if (f->flags & O_WRONLY) {
                    p->writers--;
                } else {
                    p->readers--;
                }
                if (p->readers <= 0 && p->writers <= 0) {
                    kfree(p);
                }
                kfree(f->node);
            } else if (f->node->ops && f->node->ops->close) {
                f->node->ops->close(f->node);
            }
        }
        kfree(f);
    }
}
