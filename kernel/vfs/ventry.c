//
// Created by Aaron Gill-Braun on 2023-05-25.
//

#include <kernel/vfs/ventry.h>
#include <kernel/vfs/vnode.h>

#include <kernel/mm.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <murmur3.h>

#define ASSERT(x) kassert(x)

#define MURMUR3_SEED 0xDEADBEEF

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("ventry: %s: " fmt, __func__, ##__VA_ARGS__)

static struct ventry_ops ve_default_ops = {
  .v_cleanup = NULL,
};

static hash_t ve_hash_default(cstr_t str) {
  uint64_t tmp[2] = {0, 0};
  murmur_hash_x86_128(cstr_ptr(str), (int) cstr_len(str), MURMUR3_SEED, tmp);
  return tmp[0] ^ tmp[1];
}

static bool ve_cmp_default(ventry_t *ve, cstr_t str) {
  return cstr_eq(cstr_from_str(ve->name), str);
}

//

__ref ventry_t *ve_alloc_linked(cstr_t name, vnode_t *vn) {
  ventry_t *entry = kmallocz(sizeof(ventry_t));
  entry->type = vn->type;
  entry->state = V_EMPTY;
  entry->name = str_from_cstr(name);
  if (vn->vfs) {
    entry->ops = vn->vfs->type->ve_ops;
  } else {
    entry->ops = &ve_default_ops;
  }

  mtx_init(&entry->lock, MTX_RECURSIVE, "ventry_lock");
  ref_init(&entry->refcount);

  ve_link_vnode(entry, vn);
  ve_syncvn(entry);

  DPRINTF("allocated {:+ve} and linked to {:+vn}\n", entry, vn);
  return entry;
}

void ve_link_vnode(ventry_t *ve, vnode_t *vn) {
  ASSERT(ve->type == vn->type);
  ASSERT(!VE_ISLINKED(ve))
  vn->nlink++;
  vn->flags |= VN_DIRTY;

  ve->flags |= VE_LINKED;
  ve->id = vn->id;
  ve->vn = vn_getref(vn);
}

void ve_unlink_vnode(ventry_t *ve, vnode_t *vn) {
  DPRINTF("unlinked {:ve} from {:vn}\n", ve, vn);
  ve->flags &= ~VE_LINKED;
  vn->nlink--;
  vn->flags |= VN_DIRTY;
}

void ve_shadow_mount(ventry_t *mount_ve, vnode_t *root_vn) {
  ASSERT(root_vn->v_shadow == NULL);
  ASSERT(mount_ve->chld_count == 0);

  root_vn->v_shadow = vn_moveref(&mount_ve->vn);
  root_vn->flags |= VN_ROOT;
  ve_release(&mount_ve->mount);
  if (root_vn->vfs) {
    mount_ve->mount = ve_getref(root_vn->vfs->root_ve);
  }

  mount_ve->vn = vn_moveref(&root_vn);
  mount_ve->flags |= VE_MOUNT;
  ve_syncvn(mount_ve);
}

__ref vnode_t *ve_unshadow_mount(ventry_t *mount_ve) {
  if (!VN_ISROOT(VN(mount_ve))) {
    ASSERT(VN_ISROOT(VN(mount_ve)));
  }
  if (VN(mount_ve)->v_shadow == NULL) {
    panic("no shadow vnode - tried to unshadow fs_root?");
  }

  vnode_t *root_vn = vn_moveref(&mount_ve->vn);
  mount_ve->vn = vn_moveref(&root_vn->v_shadow);
  ve_release(&mount_ve->mount);
  if (VN(mount_ve)->v_shadow == NULL) {
    // no more stacked mounts
    mount_ve->flags &= ~VE_MOUNT;
  } else {
    mount_ve->mount = ve_getref(VN(mount_ve)->vfs->root_ve);
  }

  ve_syncvn(mount_ve);
  return root_vn;
}

void ve_replace_root(ventry_t *root_ve, ventry_t *newroot_ve) {
  // unshadow the oldroot vnode mount temporarily
  vnode_t *oldroot_vn = ve_unshadow_mount(root_ve);
  // unshadow the newroot vnode from its mount ventry
  vnode_t *newroot_vn = ve_unshadow_mount(newroot_ve);

  // update the newroot root ventry parent ref
  ventry_t *newroot_root_ve = newroot_vn->vfs->root_ve;
  ve_release(&newroot_root_ve->parent);
  newroot_root_ve->parent = ve_getref(root_ve);

  // stack the newroot vnode on top of the fs root vnode
  ve_shadow_mount(root_ve, newroot_vn);
  // now stack the oldroot vnode on top of the newroot vnode
  ve_shadow_mount(root_ve, oldroot_vn);

  vn_release(&oldroot_vn);
  vn_release(&newroot_vn);
  ve_syncvn(newroot_ve);
}

void ve_add_child(ventry_t *parent, ventry_t *child) {
  ASSERT(!VE_ISMOUNT(parent));
  child->parent = ve_getref(parent);
  LIST_ADD(&parent->children, ve_getref(child), list);
  parent->chld_count++;
  if (VE_ISLINKED(child)) {
    VN(child)->parent_id = parent->id;
  }
}

void ve_remove_child(ventry_t *parent, ventry_t *child) {
  ve_release(&child->parent);
  LIST_REMOVE(&parent->children, child, list);
  ve_release(&child); // release parent->children ref
  parent->chld_count--;
}

bool ve_syncvn(ventry_t *ve) {
  if (!VE_ISLINKED(ve))
    return false;

  vnode_t *vn = VN(ve);
  ASSERT(ve->type == vn->type);

  // sync the state
  ve->state = vn->state;
  if (V_ISDEAD(ve) && V_ISDIR(ve)) {
    ASSERT(!VE_ISMOUNT(ve));
    // when a ventry enters the dead state we recursively sync all children and remove them.
    ASSERT(!VE_ISMOUNT(ve));
    ventry_t *child;
    while ((child = LIST_FIRST(&ve->children)) != NULL) {
      ve_syncvn(child);
      ve_remove_child(ve, child);
    }
    return false;
  } else if (!VE_ISMOUNT(ve) && V_ISALIVE(vn) && vn->vfs) {
    // we dont sync mounts because they are not part of the vnode's vfs
    ve->vfs_id = vn->vfs->id;
    ve->ops = vn->vfs->type->ve_ops;
  }
  return true;
}

void ve_hash(ventry_t *ve) {
  if (VE_OPS(ve)->v_hash)
    ve->hash = VE_OPS(ve)->v_hash(cstr_from_str(ve->name));
  else
    ve->hash = ve_hash_default(cstr_from_str(ve->name));
}

void ve_cleanup(__move ventry_t **veref) {
  // called when last reference is released
  ventry_t *ve = ve_moveref(veref);
  DPRINTF("!!! ventry cleanup !!! {:+ve}\n", ve);
  ASSERT(ve != NULL);
  ASSERT(ve->state == V_DEAD);
  ASSERT(ve->chld_count == 0);
  ASSERT(ref_count(&ve->refcount) == 0);

  if (VE_OPS(ve)->v_cleanup)
    VE_OPS(ve)->v_cleanup(ve);

  ve_release(&ve->parent);
  vn_release(&ve->vn);
  str_free(&ve->name);
  kfree(ve);
}

//

hash_t ve_hash_cstr(ventry_t *ve, cstr_t str) {
  if (VE_OPS(ve)->v_hash)
    return VE_OPS(ve)->v_hash(str);
  return ve_hash_default(str);
}

bool ve_cmp_cstr(ventry_t *ve, cstr_t str) {
  if (VE_OPS(ve)->v_cmp)
    return VE_OPS(ve)->v_cmp(ve, str);
  return ve_cmp_default(ve, str);
}
