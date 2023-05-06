//
// Created by Aaron Gill-Braun on 2020-10-30.
//

#include <fs.h>
#include <device.h>
#include <panic.h>
#include <printf.h>
#include <hash_map.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("fs: %s: " fmt, __func__, ##__VA_ARGS__)
// #define DPRINTF(str, args...)

MAP_TYPE_DECLARE(fs_type_t *);

static hash_map_t *fs_type_by_name;
static LIST_HEAD(struct fs_type) fs_types;
static spinlock_t fs_types_lock;


static void fs_static_init() {
  fs_type_by_name = hash_map_new();
  LIST_INIT(&fs_types);
  spin_init(&fs_types_lock);
}
STATIC_INIT(fs_static_init);

//

int fs_register_type(fs_type_t *fs_type) {
  if (fs_type->name == NULL || fs_type->sb_ops == NULL || fs_type->inode_ops == NULL ||
      fs_type->dentry_ops == NULL || fs_type->file_ops == NULL) {
    DPRINTF("invalid bus: missing one or more required field(s)\n");
    return -1;
  }

  if (hash_map_get(fs_type_by_name, fs_type->name) != NULL) {
    DPRINTF("fs type '%s' already registered\n", fs_type->name);
    return -1;
  }

  SPIN_LOCK(&fs_types_lock);
  LIST_ADD(&fs_types, fs_type, list);
  hash_map_set_c(fs_type_by_name, fs_type->name, fs_type);
  SPIN_UNLOCK(&fs_types_lock);

  kprintf("fs: registered type '%s'\n", fs_type->name);
  return 0;
}

const fs_type_t *fs_type_get(const char *name) {
  return hash_map_get(fs_type_by_name, name);
}

//
// MARK: Mounting and unmounting
//

int fs_mount(const char *source, const char *mount, const char *fs_type) {
  // 1. check if source exists and that it is a block device
  // 2. check if mount point exists and is an empty directory
  // 3. check if fs_type is registered

  // 6. call sb_mount


  unimplemented("fs_mount");
}

int fs_unmount(const char *path) {
  unimplemented("fs_unmount");
}

//
// MARK: File and directory metadata
//

int fs_stat(const char *path, struct stat *stat) {
  unimplemented("fs_stat");
}

int fs_fstat(int fd, struct stat *stat) {
  unimplemented("fs_fstat");
}

//
// MARK: File and directory manipulation
//

int fs_open(const char *path, int flags, mode_t mode) {
  unimplemented("fs_open");
}

int fs_creat(const char *path, mode_t mode) {
  unimplemented("fs_creat");
}

int fs_mkdir(const char *path, mode_t mode) {
  unimplemented("fs_mkdir");
}

int fs_mknod(const char *path, mode_t mode, dev_t dev) {
  unimplemented("fs_mknod");
}

int fs_close(int fd) {
  unimplemented("fs_close");
}

//
// MARK: File read and write operations
//


ssize_t fs_read(int fd, void *buf, size_t nbytes) {
  unimplemented("fs_read");
}

ssize_t fs_write(int fd, void *buf, size_t nbytes) {
  unimplemented("fs_write");
}

off_t fs_lseek(int fd, off_t offset, int whence) {
  unimplemented("fs_lseek");
}

ssize_t fs_readv(int fd, const struct iovec *iov, int iovcnt) {
  unimplemented("fs_readv");
}

ssize_t fs_writev(int fd, const struct iovec *iov, int iovcnt) {
  unimplemented("fs_writev");
}

ssize_t fs_preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset) {
  unimplemented("fs_preadv");
}

ssize_t fs_pwritev(int fd, const struct iovec *iov, int iovcnt, off_t offset) {
  unimplemented("fs_pwritev");
}

//
// MARK: File descriptor manipulation
//

int fs_dup(int fd) {
  unimplemented("fs_dup");
}

int fs_dup2(int fd, int fd2) {
  unimplemented("fs_dup2");
}

int fs_fcntl(int fd, int cmd, uint64_t arg) {
  unimplemented("fs_fcntl");
}

//
// MARK: Directory iteration
//

dentry_t *fs_readdir(int fd) {
  unimplemented("fs_readdir");
}

long fs_telldir(int fd) {
  unimplemented("fs_telldir");
}

void fs_seekdir(int fd, long loc) {
  unimplemented("fs_seekdir");
}

void fs_rewinddir(int fd) {
  unimplemented("fs_rewinddir");
}

//
// MARK: File and directory linking
//

int fs_link(const char *path1, const char *path2) {
  unimplemented("fs_link");
}

int fs_unlink(const char *path) {
  unimplemented("fs_unlink");
}

int fs_symlink(const char *path1, const char *path2) {
  unimplemented("fs_symlink");
}

int fs_rename(const char *oldfile, const char *newfile) {
  unimplemented("fs_rename");
}

ssize_t fs_readlink(const char *restrict path, char *restrict buf, size_t bufsize) {
  unimplemented("fs_readlink");
}

int fs_rmdir(const char *path) {
  unimplemented("fs_rmdir");
}

//
// MARK: Current working directory and file system access
//

int fs_chdir(const char *path) {
  unimplemented("fs_chdir");
}

int fs_chmod(const char *path, mode_t mode) {
  unimplemented("fs_chmod");
}

int fs_chown(const char *path, uid_t owner, gid_t group) {
  unimplemented("fs_chown");
}

char *fs_getcwd(char *buf, size_t size) {
  return NULL;
}

//
// MARK: Memory mapping
//

void *fs_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
  unimplemented("fs_mmap");
}

int fs_munmap(void *addr, size_t len) {
  unimplemented("fs_munmap");
}
