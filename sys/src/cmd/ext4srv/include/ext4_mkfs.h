#pragma once

#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_blockdev.h"
#include "ext4_fs.h"

struct ext4_mkfs_info {
	u64int len;
	u32int block_size;
	u32int blocks_per_group;
	u32int inodes_per_group;
	u32int inode_size;
	u32int inodes;
	u32int journal_blocks;
	u32int feat_ro_compat;
	u32int feat_compat;
	u32int feat_incompat;
	u32int bg_desc_reserve_blocks;
	u16int dsc_size;
	u8int uuid[UUID_SIZE];
	bool journal;
	char label[16];
};

struct fs_aux_info {
    struct ext4_sblock *sb;
    u8int *bg_desc_blk;
    struct xattr_list_element *xattrs;
    u32int first_data_block;
    u64int len_blocks;
    u32int inode_table_blocks;
    u32int groups;
    u32int bg_desc_blocks;
    u32int default_i_flags;
    u32int blocks_per_ind;
    u32int blocks_per_dind;
    u32int blocks_per_tind;
};

int create_fs_aux_info(struct fs_aux_info *aux_info, struct ext4_mkfs_info *info);
void release_fs_aux_info(struct fs_aux_info *aux_info);

int write_sblocks(struct ext4_blockdev *bd, struct fs_aux_info *aux_info, struct ext4_mkfs_info *info);

int ext4_mkfs_read_info(struct ext4_blockdev *bd, struct ext4_mkfs_info *info);

int ext4_mkfs(struct ext4_fs *fs, struct ext4_blockdev *bd,
	      struct ext4_mkfs_info *info, int fs_type);
