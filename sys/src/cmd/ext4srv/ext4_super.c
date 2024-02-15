#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_misc.h"
#include "ext4_debug.h"
#include "ext4_super.h"
#include "ext4_crc32.h"

u32int ext4_block_group_cnt(struct ext4_sblock *s)
{
	u64int blocks_count = ext4_sb_get_blocks_cnt(s);
	u32int blocks_per_group = ext4_get32(s, blocks_per_group);

	u32int block_groups_count = (u32int)(blocks_count / blocks_per_group);

	if (blocks_count % blocks_per_group)
		block_groups_count++;

	return block_groups_count;
}

u32int ext4_blocks_in_group_cnt(struct ext4_sblock *s, u32int bgid)
{
	u32int block_group_count = ext4_block_group_cnt(s);
	u32int blocks_per_group = ext4_get32(s, blocks_per_group);
	u64int total_blocks = ext4_sb_get_blocks_cnt(s);

	if (bgid < block_group_count - 1)
		return blocks_per_group;

	return (u32int)(total_blocks - ((block_group_count - 1) * blocks_per_group));
}

u32int ext4_inodes_in_group_cnt(struct ext4_sblock *s, u32int bgid)
{
	u32int block_group_count = ext4_block_group_cnt(s);
	u32int inodes_per_group = ext4_get32(s, inodes_per_group);
	u32int total_inodes = ext4_get32(s, inodes_count);

	if (bgid < block_group_count - 1)
		return inodes_per_group;

	return (total_inodes - ((block_group_count - 1) * inodes_per_group));
}

static u32int ext4_sb_csum(struct ext4_sblock *s)
{
	return ext4_crc32c(EXT4_CRC32_INIT, s, offsetof(struct ext4_sblock, checksum));
}

static bool ext4_sb_verify_csum(struct ext4_sblock *s)
{
	if (!ext4_sb_feature_ro_com(s, EXT4_FRO_COM_METADATA_CSUM))
		return true;

	if (s->checksum_type != to_le32(EXT4_CHECKSUM_CRC32C))
		return false;

	return s->checksum == to_le32(ext4_sb_csum(s));
}

void ext4_sb_set_csum(struct ext4_sblock *s)
{
	if (!ext4_sb_feature_ro_com(s, EXT4_FRO_COM_METADATA_CSUM))
		return;

	s->checksum = to_le32(ext4_sb_csum(s));
}

int ext4_sb_write(struct ext4_blockdev *bdev, struct ext4_sblock *s)
{
	ext4_sb_set_csum(s);
	return ext4_block_writebytes(bdev, EXT4_SUPERBLOCK_OFFSET, s,
				     EXT4_SUPERBLOCK_SIZE);
}

int ext4_sb_read(struct ext4_blockdev *bdev, struct ext4_sblock *s)
{
	return ext4_block_readbytes(bdev, EXT4_SUPERBLOCK_OFFSET, s,
				    EXT4_SUPERBLOCK_SIZE);
}

bool ext4_sb_check(struct ext4_sblock *s)
{
	if (ext4_get16(s, magic) != EXT4_SUPERBLOCK_MAGIC)
		return false;

	if (ext4_get32(s, inodes_count) == 0)
		return false;

	if (ext4_sb_get_blocks_cnt(s) == 0)
		return false;

	if (ext4_get32(s, blocks_per_group) == 0)
		return false;

	if (ext4_get32(s, inodes_per_group) == 0)
		return false;

	if (ext4_get16(s, inode_size) < 128)
		return false;

	if (ext4_get32(s, first_inode) < 11)
		return false;

	if (ext4_sb_get_desc_size(s) < EXT4_MIN_BLOCK_GROUP_DESCRIPTOR_SIZE)
		return false;

	if (ext4_sb_get_desc_size(s) > EXT4_MAX_BLOCK_GROUP_DESCRIPTOR_SIZE)
		return false;

	if (!ext4_sb_verify_csum(s))
		return false;

	return true;
}

static inline int is_power_of(u32int a, u32int b)
{
	while (1) {
		if (a < b)
			return 0;
		if (a == b)
			return 1;
		if ((a % b) != 0)
			return 0;
		a = a / b;
	}
}

bool ext4_sb_sparse(u32int group)
{
	if (group <= 1)
		return 1;

	if (!(group & 1))
		return 0;

	return (is_power_of(group, 7) || is_power_of(group, 5) ||
		is_power_of(group, 3));
}

bool ext4_sb_is_super_in_bg(struct ext4_sblock *s, u32int group)
{
	if (ext4_sb_feature_ro_com(s, EXT4_FRO_COM_SPARSE_SUPER) &&
	    !ext4_sb_sparse(group))
		return false;
	return true;
}

static u32int ext4_bg_num_gdb_meta(struct ext4_sblock *s, u32int group)
{
	u32int dsc_per_block =
	    ext4_sb_get_block_size(s) / ext4_sb_get_desc_size(s);

	u32int metagroup = group / dsc_per_block;
	u32int first = metagroup * dsc_per_block;
	u32int last = first + dsc_per_block - 1;

	if (group == first || group == first + 1 || group == last)
		return 1;
	return 0;
}

static u32int ext4_bg_num_gdb_nometa(struct ext4_sblock *s, u32int group)
{
	if (!ext4_sb_is_super_in_bg(s, group))
		return 0;
	u32int dsc_per_block =
	    ext4_sb_get_block_size(s) / ext4_sb_get_desc_size(s);

	u32int db_count =
	    (ext4_block_group_cnt(s) + dsc_per_block - 1) / dsc_per_block;

	if (ext4_sb_feature_incom(s, EXT4_FINCOM_META_BG))
		return ext4_sb_first_meta_bg(s);

	return db_count;
}

u32int ext4_bg_num_gdb(struct ext4_sblock *s, u32int group)
{
	u32int dsc_per_block =
	    ext4_sb_get_block_size(s) / ext4_sb_get_desc_size(s);
	u32int first_meta_bg = ext4_sb_first_meta_bg(s);
	u32int metagroup = group / dsc_per_block;

	if (!ext4_sb_feature_incom(s,EXT4_FINCOM_META_BG) ||
	    metagroup < first_meta_bg)
		return ext4_bg_num_gdb_nometa(s, group);

	return ext4_bg_num_gdb_meta(s, group);
}

u32int ext4_num_base_meta_clusters(struct ext4_sblock *s,
				     u32int block_group)
{
	u32int num;
	u32int dsc_per_block =
	    ext4_sb_get_block_size(s) / ext4_sb_get_desc_size(s);

	num = ext4_sb_is_super_in_bg(s, block_group);

	if (!ext4_sb_feature_incom(s, EXT4_FINCOM_META_BG) ||
	    block_group < ext4_sb_first_meta_bg(s) * dsc_per_block) {
		if (num) {
			num += ext4_bg_num_gdb(s, block_group);
			num += ext4_get16(s, s_reserved_gdt_blocks);
		}
	} else {
		num += ext4_bg_num_gdb(s, block_group);
	}

	u32int clustersize = 1024 << ext4_get32(s, log_cluster_size);
	u32int cluster_ratio = clustersize / ext4_sb_get_block_size(s);
	u32int v =
	    (num + cluster_ratio - 1) >> ext4_get32(s, log_cluster_size);

	return v;
}
