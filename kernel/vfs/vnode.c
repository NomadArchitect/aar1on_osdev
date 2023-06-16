//
// Created by Aaron Gill-Braun on 2023-05-20.
//

#include <vfs/vnode.h>
#include <vfs/ventry.h>
#include <vfs/vfs.h>
#include <vfs/file.h>

#include <mm.h>
#include <printf.h>


#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("vnode: %s: " fmt, __func__, ##__VA_ARGS__)

#define CHECK_SAMEDEV(vn1, vn2) if ((vn1)->vfs != (vn2)->vfs) return -EXDEV;
#define CHECK_WRITE(vn) if (VFS_ISRDONLY((vn)->vfs)) return -EROFS;
#define CHECK_DIR(vn) if ((vn)->type != V_DIR) return -ENOTDIR;
#define CHECK_NAMELEN(name) if (cstr_len(name) > NAME_MAX) return -ENAMETOOLONG;
#define CHECK_SUPPORTED(vn, op) if (!(vn)->ops->op) return -ENOTSUP;

#define make_vattr(t) ({ struct vattr __attr = {0}; __attr.type = t; __attr; })

static inline mode_t vn_to_mode(vnode_t *vnode) {
  mode_t mode = 0;
  switch (vnode->type) {
    case V_REG: mode |= S_IFREG; break;
    case V_DIR: mode |= S_IFDIR; break;
    case V_LNK: mode |= S_IFLNK; break;
    case V_BLK: mode |= S_IFBLK; break;
    case V_CHR: mode |= S_IFCHR; break;
    case V_FIFO: mode |= S_IFIFO; break;
    case V_SOCK: mode |= S_IFSOCK; break;
    default: panic("invalid vnode type");
  }
  return mode;
}

static void vn_cleanup(vnode_t *vn) {
  // called when last reference is released
  ASSERT(ref_count(&vn->refcount) == 0);
  DPRINTF("cleanup [id=%u]\n", vn->id);

  if (VN_OPS(vn)->v_cleanup)
    VN_OPS(vn)->v_cleanup(vn);
  kfree(vn);
}

//

vnode_t *vn_alloc_empty(enum vtype type) __move {
  vnode_t *vnode = kmallocz(sizeof(vnode_t));
  vnode->type = type;
  vnode->state = V_EMPTY;
  vnode->flags = 0;
  mutex_init(&vnode->lock, MUTEX_REENTRANT);
  rw_lock_init(&vnode->data_lock);
  ref_init(&vnode->refcount);
  return vn_moveref(&vnode);
}

vnode_t *vn_alloc(id_t id, struct vattr *vattr) __move {
  vnode_t *vnode = vn_alloc_empty(vattr->type);
  vnode->id = id;
  return vn_moveref(&vnode);
}

void vn_release(__move vnode_t **vnref) {
  if (ref_put(&(*vnref)->refcount, NULL)) {
    vn_cleanup(*vnref);
  }
  *vnref = NULL;
}

void vn_setdirty(vnode_t *vn) {
  vn->flags |= VN_DIRTY;
}

void vn_stat(vnode_t *vn, struct stat *statbuf) {
  memset(statbuf, 0, sizeof(struct stat));
  statbuf->st_ino = vn->id;
  statbuf->st_mode = vn_to_mode(vn);
  statbuf->st_size = (off_t) vn->size;
  statbuf->st_blocks = (blkcnt_t) vn->blocks;
  statbuf->st_nlink = vn->nlink;
  statbuf->st_rdev = vn->device ? make_dev(vn->device) : 0;
  if (vn->type == V_BLK || vn->type == V_CHR) {
    statbuf->st_dev = vn->v_dev;
    statbuf->st_dev = vn->v_dev;
  }

  // TODO: rest of the fields
}

//

int vn_open(vnode_t *vn, int flags) {
  if (!VN_OPS(vn)->v_open)
    return 0;

  // filesystem open
  int res = VN_OPS(vn)->v_open(vn, flags);
  if (res < 0) {
    return res;
  }
  return 0;
}

int vn_close(vnode_t *vn) {
  if (!VN_OPS(vn)->v_close)
    return 0;

  // filesystem close
  int res = VN_OPS(vn)->v_close(vn);
  if (res < 0) {
    return res;
  }
  return 0;
}

ssize_t vn_read(vnode_t *vn, off_t off, kio_t *kio) {
  if (!VN_OPS(vn)->v_read) return -ENOTSUP;
  if (off > vn->size) return -EOVERFLOW;
  if (off < 0) return -EINVAL;

  // filesystem read
  return VN_OPS(vn)->v_read(vn, off, kio);
}

ssize_t vn_write(vnode_t *vn, off_t off, kio_t *kio) {
  if (VFS_ISRDONLY(vn->vfs)) return -EROFS;
  if (!VN_OPS(vn)->v_write) return -ENOTSUP;
  if (off > vn->size) return -EOVERFLOW;
  if (off < 0) return -EINVAL;

  // filesystem write
  return VN_OPS(vn)->v_write(vn, off, kio);
}

int vn_map(vnode_t *vn, off_t off, struct vm_mapping *mapping) {
  if (!VN_OPS(vn)->v_map) return -ENOTSUP;
  if (off > vn->size) return -EOVERFLOW;
  if (off < 0) return -EINVAL;

  // filesystem map
  return VN_OPS(vn)->v_map(vn, off, mapping);
}

//

int vn_load(vnode_t *vn) {
  if (VN_ISLOADED(vn)) return 0;
  if (!VN_OPS(vn)->v_load) return 0;
  int res;

  if ((res = VN_OPS(vn)->v_load(vn)) < 0) {
    return res;
  }

  vn->flags |= VN_LOADED;
  return 0;
}

int vn_save(vnode_t *vn) {
  CHECK_WRITE(vn);
  if (!VN_ISDIRTY(vn)) return 0;
  if (!VN_OPS(vn)->v_save) return 0;
  int res;

  if ((res = VN_OPS(vn)->v_save(vn)) < 0) {
    return res;
  }

  vn->flags &= ~VN_DIRTY;
  return res;
}

int vn_readlink(vnode_t *vn, kio_t *kio) {
  CHECK_SUPPORTED(vn, v_readlink);
  int res;

  if (str_len(vn->v_link) == 0) {
    // read link from filesystem
    str_t link = str_alloc(vn->size);
    kio_t lnkio = kio_writeonly_from_str(link);
    if ((res = VN_OPS(vn)->v_readlink(vn, &lnkio)) < 0) {
      str_free(&link);
      return res;
    }

    // save link
    vn->v_link = link;
  }

  // copy link to kio
  kio_t lnkio = kio_writeonly_from_str(vn->v_link);
  kio_copy(&lnkio, kio);
  return 0;
}

ssize_t vn_readdir(vnode_t *vn, off_t off, kio_t *dirbuf) {
  CHECK_DIR(vn);
  CHECK_SUPPORTED(vn, v_readdir);
  if (off < 0) return -EINVAL;
  return VN_OPS(vn)->v_readdir(vn, off, dirbuf);
}

//

int vn_lookup(ventry_t *dve, vnode_t *dvn, cstr_t name, __move ventry_t **result) {
  CHECK_DIR(dvn);
  CHECK_NAMELEN(name);
  vfs_t *vfs = dvn->vfs;
  ventry_t *ve;
  int res;

  // check already loaded children
  LIST_FOR_IN(child, &dve->children, list) {
    if (ve_cmp_cstr(child, name)) {
      // TODO: revalidate ventry
      *result = ve_getref(child); // move new ref to caller
      return 0;
    }
  }

  // READ BEGIN
  if (!vfs_begin_read_op(vfs))
    return -EIO; // vfs is unmounted

  // filesystem lookup
  if ((res = VN_OPS(dvn)->v_lookup(dvn, name, &ve)) < 0) {
    vfs_end_read_op(vfs);
    return res;
  }

  assert_new_ventry_valid(ve);
  vfs_add_vnode(vfs, VN(ve));
  vfs_end_read_op(vfs);
  // READ END

  ve_syncvn(ve);
  ve_add_child(dve, ve);
  if (result)
    *result = ve_moveref(&ve);
  else
    ve_release(&ve);
  return 0;
}

int vn_create(ventry_t *dve, vnode_t *dvn, cstr_t name, mode_t mode, __move ventry_t **result) {
  CHECK_WRITE(dvn);
  CHECK_DIR(dvn);
  CHECK_NAMELEN(name);
  CHECK_SUPPORTED(dvn, v_create);
  struct vattr attr = make_vattr(V_REG);
  vfs_t *vfs = dvn->vfs;
  ventry_t *ve;
  int res;

  // WRITE BEGIN
  if (!vfs_begin_write_op(vfs))
    return -EIO; // vfs is unmounted

  // filesystem create
  if ((res = VN_OPS(dvn)->v_create(dvn, name, &attr, &ve)) < 0) {
    vfs_end_write_op(vfs);
    return res;
  }

  assert_new_ventry_valid(ve);
  vfs_add_vnode(vfs, VN(ve));
  vfs_end_write_op(vfs);
  // WRITE END

  ve_syncvn(ve);
  ve_add_child(dve, ve);
  if (result)
    *result = ve_moveref(&ve);
  else
    ve_release(&ve);
  return 0;
}

int vn_mknod(ventry_t *dve, vnode_t *dvn, cstr_t name, mode_t mode, dev_t dev, __move ventry_t **result) {
  CHECK_WRITE(dvn);
  CHECK_DIR(dvn);
  CHECK_NAMELEN(name);
  CHECK_SUPPORTED(dvn, v_mknod);
  vfs_t *vfs = dvn->vfs;
  ventry_t *ve;
  int res;

  struct vattr attr;
  if (mode & S_IFBLK) {
    attr = make_vattr(V_BLK);
  } else if (mode & S_IFCHR) {
    attr = make_vattr(V_CHR);
  } else {
    return -EINVAL;
  }

  // WRITE BEGIN
  if (!vfs_begin_write_op(vfs))
    return -EIO; // vfs is unmounted

  // filesystem mknod
  if ((res = VN_OPS(dvn)->v_mknod(dvn, name, &attr, dev, &ve)) < 0) {
    vfs_end_write_op(vfs);
    return res;
  }

  assert_new_ventry_valid(ve);
  vfs_add_vnode(vfs, VN(ve));
  vfs_end_write_op(vfs);
  // WRITE END

  ve_syncvn(ve);
  ve_add_child(dve, ve);
  if (result)
    *result = ve_moveref(&ve);
  else
    ve_release(&ve);
  return 0;
}

int vn_symlink(ventry_t *dve, vnode_t *dvn, cstr_t name, cstr_t target, __move ventry_t **result) {
  CHECK_WRITE(dvn);
  CHECK_DIR(dvn);
  CHECK_NAMELEN(name);
  CHECK_NAMELEN(target);
  CHECK_SUPPORTED(dvn, v_symlink);
  struct vattr attr = make_vattr(V_LNK);
  vfs_t *vfs = dvn->vfs;
  ventry_t *ve;
  int res;

  // WRITE BEGIN
  if (!vfs_begin_write_op(vfs))
    return -EIO; // vfs is unmounted

  // filesystem symlink
  if ((res = VN_OPS(dvn)->v_symlink(dvn, name, &attr, target, &ve)) < 0) {
    vfs_end_write_op(vfs);
    return res;
  }

  assert_new_ventry_valid(ve);
  vfs_add_vnode(vfs, VN(ve));
  vfs_end_write_op(vfs);
  // WRITE END

  ve_syncvn(ve);
  ve_add_child(dve, ve);
  if (result)
    *result = ve_moveref(&ve);
  else
    ve_release(&ve);
  return 0;
}

int vn_hardlink(ventry_t *dve, vnode_t *dvn, cstr_t name, vnode_t *target, __move ventry_t **result) {
  CHECK_SAMEDEV(dvn, target);
  CHECK_WRITE(dvn);
  CHECK_NAMELEN(name);
  CHECK_SUPPORTED(dvn, v_hardlink);
  vfs_t *vfs = dvn->vfs;
  ventry_t *ve;
  int res;

  // WRITE BEGIN
  if (!vfs_begin_write_op(vfs))
    return -EIO; // vfs is unmounted

  // filesystem hardlink
  if ((res = VN_OPS(dvn)->v_hardlink(dvn, name, target, &ve)) < 0) {
    vfs_end_write_op(vfs);
    return res;
  }

  assert_new_ventry_valid(ve);
  vfs_end_write_op(vfs);
  // WRITE END

  ve_syncvn(ve);
  ve_add_child(dve, ve);
  if (result)
    *result = ve_moveref(&ve);
  else
    ve_release(&ve);
  return 0;
}

int vn_unlink(ventry_t *dve, vnode_t *dvn, ventry_t *ve, vnode_t *vn) {
  CHECK_SAMEDEV(dvn, vn);
  CHECK_WRITE(dvn);
  CHECK_DIR(dvn);
  CHECK_SUPPORTED(dvn, v_unlink);
  vfs_t *vfs = dvn->vfs;
  int res;

  // WRITE BEGIN
  if (!vfs_begin_write_op(vfs))
    return -EIO; // vfs is unmounted

  // filesystem unlink
  if ((res = VN_OPS(dvn)->v_unlink(dvn, vn, ve)) < 0) {
    vfs_end_write_op(vfs);
    return res;
  }

  vfs_end_write_op(vfs);
  // WRITE END

  ve_remove_child(dve, ve);
  ve_unlink_vnode(ve, vn);

  if (vn->nlink == 0) {
    panic("what to do here");
  }

  return 0;
}

int vn_mkdir(ventry_t *dve, vnode_t *dvn, cstr_t name, mode_t mode, __move ventry_t **result) {
  CHECK_WRITE(dvn);
  CHECK_DIR(dvn);
  CHECK_NAMELEN(name);
  CHECK_SUPPORTED(dvn, v_mkdir);
  vfs_t *vfs = dvn->vfs;
  ventry_t *ve;
  int res;

  // WRITE BEGIN
  if (!vfs_begin_write_op(vfs))
    return -EIO; // vfs is unmounted

  // filesystem mkdir
  struct vattr attr = make_vattr(V_DIR);
  if ((res = VN_OPS(dvn)->v_mkdir(dvn, name, &attr, &ve)) < 0) {
    vfs_end_write_op(vfs);
    return res;
  }

  vfs_end_write_op(vfs);
  // WRITE END

  ve_syncvn(ve);
  ve_add_child(dve, ve);
  if (result)
    *result = ve_moveref(&ve);
  else
    ve_release(&ve);
  return 0;
}

int vn_rmdir(ventry_t *dve, vnode_t *dvn, ventry_t *ve, vnode_t *vn) {
  CHECK_DIR(vn);
  CHECK_WRITE(dvn);
  CHECK_SUPPORTED(dvn, v_rmdir);
  vfs_t *vfs = dvn->vfs;
  int res;

  // WRITE BEGIN
  if (!vfs_begin_write_op(vfs))
    return -EIO; // vfs is unmounted

  // filesystem rmdir
  if ((res = VN_OPS(dvn)->v_rmdir(dvn, vn, ve)) < 0) {
    return res;
  }

  ve_remove_child(dve, ve);
  ve_unlink_vnode(ve, vn);
  return 0;
}
