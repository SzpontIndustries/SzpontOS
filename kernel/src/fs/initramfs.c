#include <fs/initramfs.h>
#include <sched/process.h>
#include <sched/sched.h>
#include <mm/heap.h>
#include <kernel/string.h>
#include <kernel/kprint.h>

typedef struct initramfs_entry {
    vfs_node_t node;
    void *data;
    size_t capacity;
    bool is_dynamic_data;
    struct initramfs_entry **children;
    size_t child_count;
    size_t child_capacity;
} initramfs_entry_t;

static vfs_ops_t g_initramfs_file_ops;
static vfs_ops_t g_initramfs_dir_ops;
static vfs_ops_t g_initramfs_symlink_ops;

static size_t parse_octal(const char *str, size_t max_len) {
    size_t result = 0;
    for (size_t i = 0; i < max_len && str[i] >= '0' && str[i] <= '7'; i++) {
        result = (result << 3) | (str[i] - '0');
    }
    return result;
}

static ssize_t initramfs_read(vfs_node_t *node, off_t offset, size_t size, void *buffer) {
    initramfs_entry_t *entry = (initramfs_entry_t *)node;
    if (!entry || !entry->data || !buffer)
        return 0;

    if (offset >= (off_t)node->length)
        return 0;
    if (offset + size > node->length) {
        size = node->length - offset;
    }

    memcpy(buffer, (const char *)entry->data + offset, size);
    return (ssize_t)size;
}

static ssize_t initramfs_write(vfs_node_t *node, off_t offset, size_t size, const void *buffer) {
    initramfs_entry_t *entry = (initramfs_entry_t *)node;
    if (!entry || !buffer)
        return -1;

    size_t required = (size_t)offset + size;
    if (!entry->is_dynamic_data || required > entry->capacity) {
        size_t new_cap = (required > entry->capacity * 2) ? required + 512 : (entry->capacity * 2 + 512);
        void *new_data = kmalloc(new_cap);
        if (!new_data)
            return -1;

        if (entry->data && node->length > 0) {
            memcpy(new_data, entry->data, node->length);
        }
        if (entry->is_dynamic_data && entry->data) {
            kfree(entry->data);
        }

        entry->data = new_data;
        entry->capacity = new_cap;
        entry->is_dynamic_data = true;
    }

    memcpy((char *)entry->data + offset, buffer, size);
    if ((size_t)offset + size > node->length) {
        node->length = (size_t)offset + size;
    }

    return (ssize_t)size;
}

static int initramfs_truncate(vfs_node_t *node, off_t length) {
    initramfs_entry_t *entry = (initramfs_entry_t *)node;
    if (!entry || length < 0)
        return -1;

    if ((size_t)length > entry->capacity) {
        size_t new_cap = (size_t)length + 512;
        void *new_data = kzalloc(new_cap);
        if (!new_data)
            return -1;

        if (entry->data && node->length > 0) {
            memcpy(new_data, entry->data, node->length);
        }
        if (entry->is_dynamic_data && entry->data) {
            kfree(entry->data);
        }

        entry->data = new_data;
        entry->capacity = new_cap;
        entry->is_dynamic_data = true;
    }

    node->length = (size_t)length;
    return 0;
}

static struct vfs_dirent *initramfs_readdir(vfs_node_t *node, uint32_t index) {
    initramfs_entry_t *entry = (initramfs_entry_t *)node;
    if (!entry || index >= entry->child_count || !entry->children)
        return NULL;

    static vfs_dirent_t dirent;
    memset(&dirent, 0, sizeof(dirent));
    strncpy(dirent.name, entry->children[index]->node.name, sizeof(dirent.name) - 1);
    dirent.inode = entry->children[index]->node.inode;
    dirent.type = entry->children[index]->node.flags;

    return &dirent;
}

static vfs_node_t *initramfs_finddir(vfs_node_t *node, const char *name) {
    initramfs_entry_t *entry = (initramfs_entry_t *)node;
    if (!entry || !name || !entry->children)
        return NULL;

    for (size_t i = 0; i < entry->child_count; i++) {
        if (strcmp(entry->children[i]->node.name, name) == 0) {
            return &entry->children[i]->node;
        }
    }
    return NULL;
}

static int initramfs_add_child(initramfs_entry_t *parent, initramfs_entry_t *child) {
    if (!parent || !child)
        return -1;

    if (parent->child_count >= parent->child_capacity) {
        size_t new_cap = (parent->child_capacity == 0) ? 16 : (parent->child_capacity * 2);
        initramfs_entry_t **new_children = (initramfs_entry_t **)kmalloc(new_cap * sizeof(initramfs_entry_t *));
        if (!new_children)
            return -1;

        if (parent->children && parent->child_count > 0) {
            memcpy(new_children, parent->children, parent->child_count * sizeof(initramfs_entry_t *));
            kfree(parent->children);
        }

        parent->children = new_children;
        parent->child_capacity = new_cap;
    }

    parent->children[parent->child_count++] = child;
    return 0;
}

static initramfs_entry_t *create_entry(const char *name, uint32_t flags, size_t size, void *data, mode_t mode,
                                       uid_t uid, gid_t gid) {
    initramfs_entry_t *entry = (initramfs_entry_t *)kzalloc(sizeof(initramfs_entry_t));
    if (!entry)
        return NULL;

    strncpy(entry->node.name, name, sizeof(entry->node.name) - 1);
    entry->node.flags = flags;
    entry->node.length = size;
    entry->node.permissions = mode ? mode : ((flags == VFS_TYPE_DIRECTORY) ? 0755 : 0644);
    entry->node.uid = uid;
    entry->node.gid = gid;

    if (flags == VFS_TYPE_DIRECTORY) {
        entry->node.ops = &g_initramfs_dir_ops;
        entry->child_capacity = 16;
        entry->children = (initramfs_entry_t **)kzalloc(entry->child_capacity * sizeof(initramfs_entry_t *));
    } else if (flags == VFS_TYPE_SYMLINK) {
        entry->node.ops = &g_initramfs_symlink_ops;
    } else {
        entry->node.ops = &g_initramfs_file_ops;
    }

    entry->data = data;
    entry->capacity = size;
    entry->is_dynamic_data = false;
    entry->node.device_data = data;
    return entry;
}

static int initramfs_create(vfs_node_t *parent, const char *name, mode_t mode) {
    initramfs_entry_t *p = (initramfs_entry_t *)parent;
    if (!p || p->node.flags != VFS_TYPE_DIRECTORY)
        return -1;

    /* Check if already exists */
    for (size_t i = 0; i < p->child_count; i++) {
        if (strcmp(p->children[i]->node.name, name) == 0) {
            return 0; /* Already exists */
        }
    }

    process_t *curr = sched_get_current_process();
    uid_t uid = curr ? curr->euid : 0;
    gid_t gid = curr ? curr->egid : 0;

    initramfs_entry_t *child = create_entry(name, VFS_TYPE_FILE, 0, NULL, mode ? mode : 0644, uid, gid);
    if (!child)
        return -1;

    child->is_dynamic_data = true;
    return initramfs_add_child(p, child);
}

static int initramfs_mkdir(vfs_node_t *parent, const char *name, mode_t mode) {
    initramfs_entry_t *p = (initramfs_entry_t *)parent;
    if (!p || p->node.flags != VFS_TYPE_DIRECTORY)
        return -1;

    /* Check if already exists */
    for (size_t i = 0; i < p->child_count; i++) {
        if (strcmp(p->children[i]->node.name, name) == 0) {
            return 0; /* Already exists */
        }
    }

    process_t *curr = sched_get_current_process();
    uid_t uid = curr ? curr->euid : 0;
    gid_t gid = curr ? curr->egid : 0;

    initramfs_entry_t *child = create_entry(name, VFS_TYPE_DIRECTORY, 0, NULL, mode ? mode : 0755, uid, gid);
    if (!child)
        return -1;

    return initramfs_add_child(p, child);
}

static int initramfs_symlink(vfs_node_t *parent, const char *name, const char *target) {
    initramfs_entry_t *p = (initramfs_entry_t *)parent;
    if (!p || p->node.flags != VFS_TYPE_DIRECTORY || !name || !target)
        return -1;

    process_t *curr = sched_get_current_process();
    uid_t uid = curr ? curr->euid : 0;
    gid_t gid = curr ? curr->egid : 0;

    size_t tlen = strlen(target) + 1;
    char *data = (char *)kmalloc(tlen);
    if (!data)
        return -1;
    memcpy(data, target, tlen);

    initramfs_entry_t *child = create_entry(name, VFS_TYPE_SYMLINK, tlen - 1, data, 0777, uid, gid);
    if (!child) {
        kfree(data);
        return -1;
    }
    child->is_dynamic_data = true;
    child->node.device_data = data;

    return initramfs_add_child(p, child);
}

static int initramfs_link(vfs_node_t *parent, vfs_node_t *source, const char *new_name) {
    initramfs_entry_t *p = (initramfs_entry_t *)parent;
    initramfs_entry_t *src = (initramfs_entry_t *)source;
    if (!p || p->node.flags != VFS_TYPE_DIRECTORY || !src || !new_name)
        return -1;

    /* Check if already exists */
    for (size_t i = 0; i < p->child_count; i++) {
        if (strcmp(p->children[i]->node.name, new_name) == 0) {
            return -17; /* EEXIST */
        }
    }

    void *data_copy = NULL;
    size_t cap = src->capacity;
    if (cap < src->node.length)
        cap = src->node.length;
    if (cap == 0 && src->node.length == 0)
        cap = 512;

    if (src->data && src->node.length > 0) {
        data_copy = kmalloc(cap);
        if (!data_copy)
            return -12; /* ENOMEM */
        memcpy(data_copy, src->data, src->node.length);
    }

    initramfs_entry_t *child = create_entry(new_name, src->node.flags, src->node.length,
                                            data_copy, src->node.permissions, src->node.uid, src->node.gid);
    if (!child) {
        if (data_copy) kfree(data_copy);
        return -12;
    }
    child->capacity = cap;
    child->is_dynamic_data = true;

    return initramfs_add_child(p, child);
}

static ssize_t initramfs_readlink(vfs_node_t *node, char *buf, size_t bufsiz) {
    initramfs_entry_t *entry = (initramfs_entry_t *)node;
    if (!entry || !entry->data || !buf || bufsiz == 0)
        return -1;

    size_t len = strlen((const char *)entry->data);
    if (len > bufsiz)
        len = bufsiz;

    memcpy(buf, entry->data, len);
    return (ssize_t)len;
}

static int initramfs_chmod(vfs_node_t *node, mode_t mode) {
    if (!node)
        return -1;
    node->permissions = (mode & 07777);
    return 0;
}

static int initramfs_chown(vfs_node_t *node, uid_t uid, gid_t gid) {
    if (!node)
        return -1;
    if (uid != (uid_t)-1)
        node->uid = uid;
    if (gid != (gid_t)-1)
        node->gid = gid;
    return 0;
}

static int initramfs_unlink(vfs_node_t *parent, const char *name) {
    initramfs_entry_t *p = (initramfs_entry_t *)parent;
    if (!p || p->node.flags != VFS_TYPE_DIRECTORY || !name || !p->children)
        return -1;

    for (size_t i = 0; i < p->child_count; i++) {
        if (strcmp(p->children[i]->node.name, name) == 0) {
            initramfs_entry_t *target = p->children[i];

            if (target->node.flags == VFS_TYPE_DIRECTORY && target->child_count > 0) {
                return -1; /* Directory not empty */
            }

            if (target->is_dynamic_data && target->data) {
                kfree(target->data);
            }
            if (target->children) {
                kfree(target->children);
            }

            for (size_t j = i; j + 1 < p->child_count; j++) {
                p->children[j] = p->children[j + 1];
            }
            p->child_count--;
            kfree(target);
            return 0;
        }
    }

    return -2; /* ENOENT */
}

static int initramfs_rmdir(vfs_node_t *parent, const char *name) {
    initramfs_entry_t *p = (initramfs_entry_t *)parent;
    if (!p || p->node.flags != VFS_TYPE_DIRECTORY || !name || !p->children)
        return -1;

    for (size_t i = 0; i < p->child_count; i++) {
        if (strcmp(p->children[i]->node.name, name) == 0) {
            initramfs_entry_t *target = p->children[i];

            if (target->node.flags != VFS_TYPE_DIRECTORY) {
                return -20; /* ENOTDIR */
            }

            if (target->child_count > 0) {
                return -39; /* ENOTEMPTY */
            }

            if (target->children) {
                kfree(target->children);
            }

            for (size_t j = i; j + 1 < p->child_count; j++) {
                p->children[j] = p->children[j + 1];
            }
            p->child_count--;
            kfree(target);
            return 0;
        }
    }

    return -2; /* ENOENT */
}

static int initramfs_rename(vfs_node_t *old_parent, const char *old_name, vfs_node_t *new_parent,
                            const char *new_name) {
    initramfs_entry_t *old_p = (initramfs_entry_t *)old_parent;
    initramfs_entry_t *new_p = (initramfs_entry_t *)new_parent;

    if (!old_p || !new_p || !old_name || !new_name || !old_p->children)
        return -1;

    if (old_p == new_p && strcmp(old_name, new_name) == 0) {
        return 0;
    }

    /* Find target child in old_parent */
    initramfs_entry_t *target = NULL;
    size_t target_idx = 0;
    for (size_t i = 0; i < old_p->child_count; i++) {
        if (strcmp(old_p->children[i]->node.name, old_name) == 0) {
            target = old_p->children[i];
            target_idx = i;
            break;
        }
    }

    if (!target)
        return -2; /* ENOENT */

    /* Remove from old_parent */
    for (size_t j = target_idx; j + 1 < old_p->child_count; j++) {
        old_p->children[j] = old_p->children[j + 1];
    }
    old_p->child_count--;

    /* If destination already exists in new_parent, remove it */
    initramfs_unlink(&new_p->node, new_name);

    /* Add to new_parent */
    if (initramfs_add_child(new_p, target) != 0) {
        return -1;
    }

    /* Update entry name */
    strncpy(target->node.name, new_name, sizeof(target->node.name) - 1);
    target->node.name[sizeof(target->node.name) - 1] = '\0';

    return 0;
}

static initramfs_entry_t *add_path_to_tree(initramfs_entry_t *root, const char *path, uint32_t flags, size_t size,
                                           void *data, mode_t mode, uid_t uid, gid_t gid) {
    char clean_path[256];
    strncpy(clean_path, path, sizeof(clean_path) - 1);
    clean_path[sizeof(clean_path) - 1] = '\0';

    char *p = clean_path;
    while (*p == '/')
        p++;

    size_t len = strlen(p);
    while (len > 0 && p[len - 1] == '/') {
        p[--len] = '\0';
    }

    if (len == 0)
        return root;

    initramfs_entry_t *curr = root;
    char *token = p;
    char *slash;

    while (token && *token) {
        slash = strchr(token, '/');
        if (slash) {
            *slash = '\0';
        }

        /* Check if child exists */
        initramfs_entry_t *next = NULL;
        if (curr->children) {
            for (size_t i = 0; i < curr->child_count; i++) {
                if (strcmp(curr->children[i]->node.name, token) == 0) {
                    next = curr->children[i];
                    break;
                }
            }
        }

        if (!next) {
            uint32_t entry_flags = (slash != NULL) ? VFS_TYPE_DIRECTORY : flags;
            size_t entry_size = (slash != NULL) ? 0 : size;
            void *entry_data = (slash != NULL) ? NULL : data;
            mode_t entry_mode = (slash != NULL) ? 0755 : mode;

            next = create_entry(token, entry_flags, entry_size, entry_data, entry_mode, uid, gid);
            if (next) {
                initramfs_add_child(curr, next);
            }
        }

        curr = next;
        if (slash) {
            token = slash + 1;
        } else {
            break;
        }
    }

    return curr;
}

vfs_node_t *initramfs_init(void *archive_ptr, size_t archive_size) {
    if (!archive_ptr || archive_size < 512)
        return NULL;

    g_initramfs_file_ops.read = initramfs_read;
    g_initramfs_file_ops.write = initramfs_write;
    g_initramfs_file_ops.open = NULL;
    g_initramfs_file_ops.close = NULL;
    g_initramfs_file_ops.readdir = NULL;
    g_initramfs_file_ops.finddir = NULL;
    g_initramfs_file_ops.create = NULL;
    g_initramfs_file_ops.mkdir = NULL;
    g_initramfs_file_ops.chmod = initramfs_chmod;
    g_initramfs_file_ops.chown = initramfs_chown;
    g_initramfs_file_ops.truncate = initramfs_truncate;

    g_initramfs_dir_ops.read = NULL;
    g_initramfs_dir_ops.write = NULL;
    g_initramfs_dir_ops.open = NULL;
    g_initramfs_dir_ops.close = NULL;
    g_initramfs_dir_ops.readdir = initramfs_readdir;
    g_initramfs_dir_ops.finddir = initramfs_finddir;
    g_initramfs_dir_ops.create = initramfs_create;
    g_initramfs_dir_ops.mkdir = initramfs_mkdir;
    g_initramfs_dir_ops.chmod = initramfs_chmod;
    g_initramfs_dir_ops.chown = initramfs_chown;
    g_initramfs_dir_ops.unlink = initramfs_unlink;
    g_initramfs_dir_ops.rmdir = initramfs_rmdir;
    g_initramfs_dir_ops.rename = initramfs_rename;
    g_initramfs_dir_ops.symlink = initramfs_symlink;
    g_initramfs_dir_ops.link = initramfs_link;

    g_initramfs_symlink_ops.read = NULL;
    g_initramfs_symlink_ops.write = NULL;
    g_initramfs_symlink_ops.readlink = initramfs_readlink;
    g_initramfs_symlink_ops.chmod = initramfs_chmod;
    g_initramfs_symlink_ops.chown = initramfs_chown;

    initramfs_entry_t *root = create_entry("/", VFS_TYPE_DIRECTORY, 0, NULL, 0755, 0, 0);

    uint8_t *ptr = (uint8_t *)archive_ptr;
    uint8_t *end = ptr + archive_size;
    size_t file_count = 0;

    while (ptr + 512 <= end) {
        ustar_header_t *hdr = (ustar_header_t *)ptr;
        if (hdr->name[0] == '\0') {
            break; /* Zero block indicates end of archive */
        }

        size_t raw_tar_size = parse_octal(hdr->size, sizeof(hdr->size));
        size_t file_size = raw_tar_size;
        mode_t mode = (mode_t)parse_octal(hdr->mode, sizeof(hdr->mode));
        uid_t uid = (uid_t)parse_octal(hdr->uid, sizeof(hdr->uid));
        gid_t gid = (gid_t)parse_octal(hdr->gid, sizeof(hdr->gid));
        uint32_t flags = (hdr->typeflag == '5') ? VFS_TYPE_DIRECTORY : VFS_TYPE_FILE;
        void *file_data = NULL;
        bool is_dynamic = false;

        if (hdr->typeflag == '2' || hdr->typeflag == '1') {
            flags = VFS_TYPE_SYMLINK;
            /* In USTAR, symlink/hardlink target is in hdr->linkname (up to 100 bytes).
             * Allocate a null-terminated string so it can be safely read as target. */
            char *link_target = (char *)kmalloc(101);
            if (link_target) {
                memcpy(link_target, hdr->linkname, 100);
                link_target[100] = '\0';
                file_data = link_target;
                file_size = strlen(link_target);
                is_dynamic = true;
            }
        } else {
            file_data = (void *)(ptr + 512);
        }

        char full_name[256];
        if (hdr->prefix[0] != '\0') {
            size_t plen = strnlen(hdr->prefix, 155);
            size_t nlen = strnlen(hdr->name, 100);
            if (plen + 1 + nlen < sizeof(full_name)) {
                memcpy(full_name, hdr->prefix, plen);
                full_name[plen] = '/';
                memcpy(full_name + plen + 1, hdr->name, nlen);
                full_name[plen + 1 + nlen] = '\0';
            } else {
                strncpy(full_name, hdr->name, 100);
                full_name[100] = '\0';
            }
        } else {
            size_t nlen = strnlen(hdr->name, 100);
            memcpy(full_name, hdr->name, nlen);
            full_name[nlen] = '\0';
        }

        initramfs_entry_t *entry = add_path_to_tree(root, full_name, flags, file_size, file_data, mode, uid, gid);
        if (entry && is_dynamic) {
            entry->is_dynamic_data = true;
            entry->node.device_data = file_data;
        }
        file_count++;

        size_t block_count = DIV_ROUND_UP(raw_tar_size, 512);
        ptr += 512 + block_count * 512;
    }

    klog_info("Initramfs parsed: %lu files/directories unpacked into VFS", file_count);
    return &root->node;
}
