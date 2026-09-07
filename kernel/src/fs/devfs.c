#include <fs/devfs.h>
#include <drivers/serial.h>
#include <drivers/framebuffer.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <drivers/ps2_mouse.h>
#include <drivers/speaker.h>
#include <drivers/tty.h>
#include <drivers/drm.h>
#include <sched/sched.h>
#include <sched/process.h>
#include <kernel/signal.h>
#include <mm/heap.h>
#include <kernel/string.h>
#include <kernel/kprint.h>

#define MAX_DEVFS_ENTRIES 128
#define MAX_DIR_ENTRIES 32

typedef struct devfs_entry {
    char name[32];
    vfs_node_t *node;
} devfs_entry_t;

typedef struct devfs_dir_data {
    devfs_entry_t entries[MAX_DIR_ENTRIES];
    size_t count;
} devfs_dir_data_t;

static vfs_node_t *g_devfs_root = NULL;
static devfs_entry_t g_devices[MAX_DEVFS_ENTRIES];
static size_t g_device_count = 0;

static vfs_ops_t g_devfs_dir_ops;
static vfs_ops_t g_null_ops;
static vfs_ops_t g_zero_ops;
static vfs_ops_t g_serial_ops;
static vfs_ops_t g_tty_ops;
static vfs_ops_t g_psaux_ops;
static vfs_ops_t g_speaker_ops;
static vfs_ops_t g_block_ops;
static vfs_ops_t g_drm_ops;
static vfs_ops_t g_drm_render_ops;

/* /dev/null */
static ssize_t devfs_null_read(vfs_node_t *node, off_t offset, size_t size, void *buffer) {
    UNUSED(node);
    UNUSED(offset);
    UNUSED(size);
    UNUSED(buffer);
    return 0; /* EOF */
}

static ssize_t devfs_null_write(vfs_node_t *node, off_t offset, size_t size, const void *buffer) {
    UNUSED(node);
    UNUSED(offset);
    UNUSED(buffer);
    return (ssize_t)size; /* Discarded */
}

/* /dev/zero */
static ssize_t devfs_zero_read(vfs_node_t *node, off_t offset, size_t size, void *buffer) {
    UNUSED(node);
    UNUSED(offset);
    if (buffer)
        memset(buffer, 0, size);
    return (ssize_t)size;
}

static ssize_t devfs_zero_write(vfs_node_t *node, off_t offset, size_t size, const void *buffer) {
    UNUSED(node);
    UNUSED(offset);
    UNUSED(buffer);
    return (ssize_t)size;
}

/* /dev/serial */
static ssize_t devfs_serial_read(vfs_node_t *node, off_t offset, size_t size, void *buffer) {
    UNUSED(node);
    UNUSED(offset);
    if (!buffer || size == 0)
        return 0;
    char *buf = (char *)buffer;
    for (size_t i = 0; i < size; i++) {
        buf[i] = serial_getc();
    }
    return (ssize_t)size;
}

static ssize_t devfs_serial_write(vfs_node_t *node, off_t offset, size_t size, const void *buffer) {
    UNUSED(node);
    UNUSED(offset);
    if (buffer && size > 0) {
        serial_write((const char *)buffer, size);
    }
    return (ssize_t)size;
}

/* /dev/tty & /dev/console */
static ssize_t devfs_tty_read(vfs_node_t *node, off_t offset, size_t size, void *buffer) {
    UNUSED(node);
    UNUSED(offset);
    return tty_read(buffer, size);
}

static ssize_t devfs_tty_write(vfs_node_t *node, off_t offset, size_t size, const void *buffer) {
    UNUSED(node);
    UNUSED(offset);
    return tty_write(buffer, size);
}

static int devfs_tty_open(vfs_node_t *node, uint32_t flags) {
    UNUSED(flags);
    UNUSED(node);
    return 0;
}

static int devfs_tty_ioctl(vfs_node_t *node, uint64_t request, uintptr_t arg) {
    UNUSED(node);
    return tty_ioctl(request, (void *)arg);
}

/* /dev/psaux (PS/2 Mouse) */
static ssize_t devfs_psaux_read(vfs_node_t *node, off_t offset, size_t size, void *buffer) {
    UNUSED(node);
    UNUSED(offset);
    return ps2_mouse_devfs_read(buffer, size);
}

static ssize_t devfs_psaux_write(vfs_node_t *node, off_t offset, size_t size, const void *buffer) {
    UNUSED(node);
    UNUSED(offset);
    UNUSED(buffer);
    return (ssize_t)size;
}

/* /dev/mouse (Universal Mouse) */
static ssize_t devfs_mouse_read(vfs_node_t *node, off_t offset, size_t size, void *buffer) {
    UNUSED(node);
    UNUSED(offset);
    return mouse_devfs_read(buffer, size);
}

static ssize_t devfs_mouse_write(vfs_node_t *node, off_t offset, size_t size, const void *buffer) {
    UNUSED(node);
    UNUSED(offset);
    UNUSED(buffer);
    return (ssize_t)size;
}

/* /dev/speaker */
static ssize_t devfs_speaker_write(vfs_node_t *node, off_t offset, size_t size, const void *buffer) {
    UNUSED(node);
    UNUSED(offset);
    if (!buffer || size < sizeof(uint32_t))
        return 0;
    uint32_t freq = *(const uint32_t *)buffer;
    speaker_beep(freq, 100);
    return (ssize_t)size;
}

/* /dev/dri/card0 & DRM operations */
static int devfs_drm_ioctl(vfs_node_t *node, uint64_t request, uintptr_t arg) {
    UNUSED(node);
    return drm_ioctl(request, (void *)arg);
}

static int devfs_drm_render_ioctl(vfs_node_t *node, uint64_t request, uintptr_t arg) {
    UNUSED(node);
    return drm_render_ioctl(request, (void *)arg);
}

static ssize_t devfs_drm_read(vfs_node_t *node, off_t offset, size_t size, void *buffer) {
    UNUSED(node);
    UNUSED(offset);
    return drm_read(buffer, size);
}

static int devfs_drm_mmap(vfs_node_t *node, void *addr, size_t length, int prot, int flags, off_t offset, void **out_vaddr) {
    UNUSED(node);
    return drm_mmap(addr, length, prot, flags, offset, out_vaddr);
}

/* Block device read/write */
static ssize_t devfs_block_read(vfs_node_t *node, off_t offset, size_t size, void *buffer) {
    block_device_t *bdev = (block_device_t *)node->device_data;
    if (!bdev || !buffer || size == 0)
        return 0;

    uint64_t start_sector = offset / bdev->sector_size;
    uint32_t num_sectors = (uint32_t)((size + bdev->sector_size - 1) / bdev->sector_size);

    uint8_t *tmp_buf = kmalloc(num_sectors * bdev->sector_size);
    if (!tmp_buf)
        return -1;

    if (bdev->read_blocks(bdev, start_sector, num_sectors, tmp_buf) != 0) {
        kfree(tmp_buf);
        return -1;
    }

    size_t in_sector_offset = offset % bdev->sector_size;
    memcpy(buffer, tmp_buf + in_sector_offset, size);
    kfree(tmp_buf);
    return (ssize_t)size;
}

static ssize_t devfs_block_write(vfs_node_t *node, off_t offset, size_t size, const void *buffer) {
    block_device_t *bdev = (block_device_t *)node->device_data;
    if (!bdev || !buffer || size == 0)
        return 0;

    uint64_t start_sector = offset / bdev->sector_size;
    uint32_t num_sectors = (uint32_t)((size + bdev->sector_size - 1) / bdev->sector_size);

    uint8_t *tmp_buf = kmalloc(num_sectors * bdev->sector_size);
    if (!tmp_buf)
        return -1;

    if (bdev->read_blocks(bdev, start_sector, num_sectors, tmp_buf) != 0) {
        kfree(tmp_buf);
        return -1;
    }

    size_t in_sector_offset = offset % bdev->sector_size;
    memcpy(tmp_buf + in_sector_offset, buffer, size);

    if (bdev->write_blocks(bdev, start_sector, num_sectors, tmp_buf) != 0) {
        kfree(tmp_buf);
        return -1;
    }

    kfree(tmp_buf);
    return (ssize_t)size;
}

/* DevFS directory operations */
static struct vfs_dirent *devfs_readdir(vfs_node_t *node, uint32_t index) {
    if (!node)
        return NULL;

    static vfs_dirent_t dirent;
    memset(&dirent, 0, sizeof(dirent));

    if (node == g_devfs_root) {
        if (index >= g_device_count)
            return NULL;
        const char *entry_name = g_devices[index].name[0] ? g_devices[index].name : g_devices[index].node->name;
        strncpy(dirent.name, entry_name, sizeof(dirent.name) - 1);
        dirent.inode = g_devices[index].node->inode;
        dirent.type = g_devices[index].node->flags;
        return &dirent;
    }

    devfs_dir_data_t *dir_data = (devfs_dir_data_t *)node->device_data;
    if (!dir_data || index >= dir_data->count)
        return NULL;

    const char *dir_entry_name = dir_data->entries[index].name[0] ? dir_data->entries[index].name : dir_data->entries[index].node->name;
    strncpy(dirent.name, dir_entry_name, sizeof(dirent.name) - 1);
    dirent.inode = dir_data->entries[index].node->inode;
    dirent.type = dir_data->entries[index].node->flags;
    return &dirent;
}

static vfs_node_t *devfs_finddir(vfs_node_t *node, const char *name) {
    if (!node || !name)
        return NULL;

    if (node == g_devfs_root) {
        for (size_t i = 0; i < g_device_count; i++) {
            if (strcmp(g_devices[i].name, name) == 0 || strcmp(g_devices[i].node->name, name) == 0) {
                return g_devices[i].node;
            }
        }
        return NULL;
    }

    devfs_dir_data_t *dir_data = (devfs_dir_data_t *)node->device_data;
    if (dir_data) {
        for (size_t i = 0; i < dir_data->count; i++) {
            if (strcmp(dir_data->entries[i].name, name) == 0 || strcmp(dir_data->entries[i].node->name, name) == 0) {
                return dir_data->entries[i].node;
            }
        }
    }
    return NULL;
}

vfs_node_t *devfs_mkdir(const char *name) {
    if (!name)
        return NULL;

    /* Check if already exists */
    vfs_node_t *existing = devfs_finddir(g_devfs_root, name);
    if (existing)
        return existing;

    vfs_node_t *dir_node = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    if (!dir_node)
        return NULL;

    strncpy(dir_node->name, name, sizeof(dir_node->name) - 1);
    dir_node->flags = VFS_TYPE_DIRECTORY;
    dir_node->permissions = 0755;
    dir_node->ops = &g_devfs_dir_ops;

    devfs_dir_data_t *data = (devfs_dir_data_t *)kzalloc(sizeof(devfs_dir_data_t));
    dir_node->device_data = data;

    devfs_register_device(name, dir_node);
    return dir_node;
}

int devfs_register_device_in_dir(vfs_node_t *dir, const char *name, vfs_node_t *node) {
    if (!dir || !name || !node)
        return -1;

    devfs_dir_data_t *data = (devfs_dir_data_t *)dir->device_data;
    if (!data || data->count >= MAX_DIR_ENTRIES)
        return -1;

    strncpy(data->entries[data->count].name, name, sizeof(data->entries[data->count].name) - 1);
    strncpy(node->name, name, sizeof(node->name) - 1);
    if (!node->permissions) {
        node->permissions = 0666;
    }
    data->entries[data->count++].node = node;
    return 0;
}

int devfs_register_device(const char *name, vfs_node_t *node) {
    if (!name || !node || g_device_count >= MAX_DEVFS_ENTRIES)
        return -1;

    strncpy(g_devices[g_device_count].name, name, sizeof(g_devices[g_device_count].name) - 1);
    strncpy(node->name, name, sizeof(node->name) - 1);
    if (!node->permissions) {
        node->permissions = 0666;
    }
    g_devices[g_device_count++].node = node;
    return 0;
}

int devfs_register_device_path(const char *path, vfs_node_t *node) {
    if (!path || !node)
        return -1;

    const char *slash = strchr(path, '/');
    if (!slash) {
        return devfs_register_device(path, node);
    }

    char dir_name[32];
    size_t dlen = (size_t)(slash - path);
    if (dlen >= sizeof(dir_name))
        dlen = sizeof(dir_name) - 1;
    memcpy(dir_name, path, dlen);
    dir_name[dlen] = '\0';

    const char *file_name = slash + 1;

    vfs_node_t *dir = devfs_mkdir(dir_name);
    if (!dir)
        return -1;

    return devfs_register_device_in_dir(dir, file_name, node);
}

int devfs_unregister_device(const char *name) {
    if (!name)
        return -1;
    for (size_t i = 0; i < g_device_count; i++) {
        if (g_devices[i].node && strcmp(g_devices[i].node->name, name) == 0) {
            vfs_node_t *node = g_devices[i].node;
            g_devices[i] = g_devices[g_device_count - 1];
            g_device_count--;
            kfree(node);
            return 0;
        }
    }
    return -1;
}

int devfs_register_block_device(block_device_t *bdev) {
    if (!bdev)
        return -1;

    vfs_node_t *node = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    node->flags = VFS_TYPE_BLOCKDEVICE;
    node->length = bdev->sector_count * bdev->sector_size;
    node->permissions = 0660;
    node->uid = 0;
    node->gid = 0;
    node->device_data = bdev;
    node->ops = &g_block_ops;

    return devfs_register_device(bdev->name, node);
}

void devfs_init(void) {
    g_devfs_dir_ops.readdir = devfs_readdir;
    g_devfs_dir_ops.finddir = devfs_finddir;

    g_null_ops.read = devfs_null_read;
    g_null_ops.write = devfs_null_write;

    g_zero_ops.read = devfs_zero_read;
    g_zero_ops.write = devfs_zero_write;

    g_serial_ops.read = devfs_serial_read;
    g_serial_ops.write = devfs_serial_write;

    g_tty_ops.read = devfs_tty_read;
    g_tty_ops.write = devfs_tty_write;
    g_tty_ops.open = devfs_tty_open;
    g_tty_ops.ioctl = devfs_tty_ioctl;

    g_psaux_ops.read = devfs_psaux_read;
    g_psaux_ops.write = devfs_psaux_write;

    static vfs_ops_t g_mouse_ops;
    g_mouse_ops.read = devfs_mouse_read;
    g_mouse_ops.write = devfs_mouse_write;

    g_speaker_ops.write = devfs_speaker_write;

    g_block_ops.read = devfs_block_read;
    g_block_ops.write = devfs_block_write;

    g_drm_ops.ioctl = devfs_drm_ioctl;
    g_drm_ops.mmap = devfs_drm_mmap;
    g_drm_ops.read = devfs_drm_read;

    g_drm_render_ops.ioctl = devfs_drm_render_ioctl;
    g_drm_render_ops.mmap = devfs_drm_mmap;
    g_drm_render_ops.read = devfs_drm_read;

    g_devfs_root = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    strcpy(g_devfs_root->name, "dev");
    g_devfs_root->flags = VFS_TYPE_DIRECTORY;
    g_devfs_root->permissions = 0755;
    g_devfs_root->ops = &g_devfs_dir_ops;

    /* Register standard character device nodes */
    vfs_node_t *null_dev = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    null_dev->flags = VFS_TYPE_CHARDEVICE;
    null_dev->permissions = 0666;
    null_dev->ops = &g_null_ops;
    devfs_register_device("null", null_dev);

    vfs_node_t *zero_dev = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    zero_dev->flags = VFS_TYPE_CHARDEVICE;
    zero_dev->permissions = 0666;
    zero_dev->ops = &g_zero_ops;
    devfs_register_device("zero", zero_dev);

    vfs_node_t *serial_dev = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    serial_dev->flags = VFS_TYPE_CHARDEVICE;
    serial_dev->permissions = 0666;
    serial_dev->ops = &g_serial_ops;
    devfs_register_device("serial", serial_dev);

    vfs_node_t *tty_dev = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    tty_dev->flags = VFS_TYPE_CHARDEVICE;
    tty_dev->permissions = 0666;
    tty_dev->ops = &g_tty_ops;
    devfs_register_device("tty", tty_dev);

    vfs_node_t *console_dev = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    console_dev->flags = VFS_TYPE_CHARDEVICE;
    console_dev->permissions = 0666;
    console_dev->ops = &g_tty_ops;
    devfs_register_device("console", console_dev);

    vfs_node_t *psaux_dev = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    psaux_dev->flags = VFS_TYPE_CHARDEVICE;
    psaux_dev->permissions = 0660;
    psaux_dev->ops = &g_psaux_ops;
    devfs_register_device("psaux", psaux_dev);

    vfs_node_t *mouse_dev = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    mouse_dev->flags = VFS_TYPE_CHARDEVICE;
    mouse_dev->permissions = 0660;
    mouse_dev->ops = &g_mouse_ops;
    devfs_register_device("mouse", mouse_dev);

    vfs_node_t *speaker_dev = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    speaker_dev->flags = VFS_TYPE_CHARDEVICE;
    speaker_dev->permissions = 0666;
    speaker_dev->ops = &g_speaker_ops;
    devfs_register_device("speaker", speaker_dev);

    /* Initialize and register DRM / KMS device nodes */
    drm_init();

    vfs_node_t *card0_dev = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    card0_dev->flags = VFS_TYPE_CHARDEVICE;
    card0_dev->permissions = 0666;
    card0_dev->ops = &g_drm_ops;
    devfs_register_device_path("dri/card0", card0_dev);

    vfs_node_t *render_dev = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    render_dev->flags = VFS_TYPE_CHARDEVICE;
    render_dev->permissions = 0666;
    render_dev->ops = &g_drm_render_ops;
    devfs_register_device_path("dri/renderD128", render_dev);

    vfs_node_t *ctrl_dev = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    ctrl_dev->flags = VFS_TYPE_CHARDEVICE;
    ctrl_dev->permissions = 0666;
    ctrl_dev->ops = &g_drm_ops;
    devfs_register_device_path("dri/controlD64", ctrl_dev);

    /* Aliases */
    vfs_node_t *card0_alias = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    card0_alias->flags = VFS_TYPE_CHARDEVICE;
    card0_alias->permissions = 0666;
    card0_alias->ops = &g_drm_ops;
    devfs_register_device("card0", card0_alias);

    vfs_node_t *render_alias = (vfs_node_t *)kzalloc(sizeof(vfs_node_t));
    render_alias->flags = VFS_TYPE_CHARDEVICE;
    render_alias->permissions = 0666;
    render_alias->ops = &g_drm_render_ops;
    devfs_register_device("renderD128", render_alias);

    vfs_mount("/dev", g_devfs_root);
    klog_info("DevFS mounted at /dev with devices: null, zero, serial, tty, console, psaux, mouse, speaker, dri/card0, dri/renderD128");
}
