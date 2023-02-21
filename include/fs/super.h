//
// Created by Aaron Gill-Braun on 2021-07-16.
//

#ifndef FS_SUPER_H
#define FS_SUPER_H

#include <fs_types.h>

// ============ Virtual API ============

super_block_t *sb_alloc(fs_type_t *fs_type);
void sb_free(super_block_t *sb);
int sb_takeown(super_block_t *sb, inode_t *inode);
int sb_add_inode(super_block_t *sb, inode_t *inode);
int sb_remove_inode(super_block_t *sb, inode_t *inode);

// ============= Operations =============

int sb_mount(dentry_t *mount, inode_t *root, fs_type_t *fs_type);
int sb_unmount(super_block_t *sb);
int sb_write(super_block_t *sb);
int sb_read_inode(super_block_t *sb, inode_t *inode);
int sb_write_inode(super_block_t *sb, inode_t *inode);
int sb_alloc_inode(super_block_t *sb, inode_t *inode);
int sb_delete_inode(super_block_t *sb, inode_t *inode);

#endif
