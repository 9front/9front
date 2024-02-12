#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_misc.h"
#include "ext4_debug.h"
#include "ext4_inode.h"
#include "ext4_super.h"

/**@brief  Compute number of bits for block count.
 * @param block_size Filesystem block_size
 * @return Number of bits
 */
static u32int ext4_inode_block_bits_count(u32int block_size)
{
	u32int bits = 8;
	u32int size = block_size;

	do {
		bits++;
		size = size >> 1;
	} while (size > 256);

	return bits;
}

u32int ext4_inode_get_mode(struct ext4_sblock *sb, struct ext4_inode *inode)
{
	u32int v = to_le16(inode->mode);

	if (ext4_get32(sb, creator_os) == EXT4_SUPERBLOCK_OS_HURD) {
		v |= ((u32int)to_le16(inode->osd2.hurd2.mode_high)) << 16;
	}

	return v;
}

void ext4_inode_set_mode(struct ext4_sblock *sb, struct ext4_inode *inode,
			 u32int mode)
{
	inode->mode = to_le16((mode << 16) >> 16);

	if (ext4_get32(sb, creator_os) == EXT4_SUPERBLOCK_OS_HURD)
		inode->osd2.hurd2.mode_high = to_le16(mode >> 16);
}

u32int ext4_inode_get_uid(struct ext4_inode *inode)
{
	return to_le32(inode->uid);
}

void ext4_inode_set_uid(struct ext4_inode *inode, u32int uid)
{
	inode->uid = to_le32(uid);
}

u64int ext4_inode_get_size(struct ext4_sblock *sb, struct ext4_inode *inode)
{
	u64int v = to_le32(inode->size_lo);

	if ((ext4_get32(sb, rev_level) > 0) &&
	    (ext4_inode_is_type(sb, inode, EXT4_INODE_MODE_FILE)))
		v |= ((u64int)to_le32(inode->size_hi)) << 32;

	return v;
}

void ext4_inode_set_size(struct ext4_inode *inode, u64int size)
{
	inode->size_lo = to_le32((size << 32) >> 32);
	inode->size_hi = to_le32(size >> 32);
}

u32int ext4_inode_get_csum(struct ext4_sblock *sb, struct ext4_inode *inode)
{
	u16int inode_size = ext4_get16(sb, inode_size);
	u32int v = to_le16(inode->osd2.linux2.checksum_lo);

	if (inode_size > EXT4_GOOD_OLD_INODE_SIZE)
		v |= ((u32int)to_le16(inode->checksum_hi)) << 16;

	return v;
}

void ext4_inode_set_csum(struct ext4_sblock *sb, struct ext4_inode *inode,
			u32int checksum)
{
	u16int inode_size = ext4_get16(sb, inode_size);
	inode->osd2.linux2.checksum_lo =
		to_le16((checksum << 16) >> 16);

	if (inode_size > EXT4_GOOD_OLD_INODE_SIZE)
		inode->checksum_hi = to_le16(checksum >> 16);

}

u32int ext4_inode_get_access_time(struct ext4_inode *inode)
{
	return to_le32(inode->access_time);
}
void ext4_inode_set_access_time(struct ext4_inode *inode, u32int time)
{
	inode->access_time = to_le32(time);
}

u32int ext4_inode_get_change_inode_time(struct ext4_inode *inode)
{
	return to_le32(inode->change_inode_time);
}
void ext4_inode_set_change_inode_time(struct ext4_inode *inode, u32int time)
{
	inode->change_inode_time = to_le32(time);
}

u32int ext4_inode_get_modif_time(struct ext4_inode *inode)
{
	return to_le32(inode->modification_time);
}

void ext4_inode_set_modif_time(struct ext4_inode *inode, u32int time)
{
	inode->modification_time = to_le32(time);
}

u32int ext4_inode_get_del_time(struct ext4_inode *inode)
{
	return to_le32(inode->deletion_time);
}

void ext4_inode_set_del_time(struct ext4_inode *inode, u32int time)
{
	inode->deletion_time = to_le32(time);
}

u32int ext4_inode_get_creation_time(struct ext4_inode *inode)
{
	return to_le32(inode->crtime);
}

u32int ext4_inode_get_gid(struct ext4_inode *inode)
{
	return to_le32(inode->gid);
}
void ext4_inode_set_gid(struct ext4_inode *inode, u32int gid)
{
	inode->gid = to_le32(gid);
}

u16int ext4_inode_get_links_cnt(struct ext4_inode *inode)
{
	return to_le16(inode->links_count);
}
void ext4_inode_set_links_cnt(struct ext4_inode *inode, u16int cnt)
{
	inode->links_count = to_le16(cnt);
}

u64int ext4_inode_get_blocks_count(struct ext4_sblock *sb,
				     struct ext4_inode *inode)
{
	u64int cnt = to_le32(inode->blocks_count_lo);

	if (ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_HUGE_FILE)) {

		/* 48-bit field */
		cnt |= (u64int)to_le16(inode->osd2.linux2.blocks_high) << 32;

		if (ext4_inode_has_flag(inode, EXT4_INODE_FLAG_HUGE_FILE)) {

			u32int block_count = ext4_sb_get_block_size(sb);
			u32int b = ext4_inode_block_bits_count(block_count);
			return cnt << (b - 9);
		}
	}

	return cnt;
}

int ext4_inode_set_blocks_count(struct ext4_sblock *sb,
				struct ext4_inode *inode, u64int count)
{
	/* 32-bit maximum */
	u64int max = 0;
	max = ~max >> 32;

	if (count <= max) {
		inode->blocks_count_lo = to_le32((u32int)count);
		inode->osd2.linux2.blocks_high = 0;
		ext4_inode_clear_flag(inode, EXT4_INODE_FLAG_HUGE_FILE);

		return 0;
	}

	/* Check if there can be used huge files (many blocks) */
	if (!ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_HUGE_FILE)) {
		werrstr(Einval);
		return -1;
	}

	/* 48-bit maximum */
	max = 0;
	max = ~max >> 16;

	if (count <= max) {
		inode->blocks_count_lo = to_le32((u32int)count);
		inode->osd2.linux2.blocks_high = to_le16((u16int)(count >> 32));
		ext4_inode_clear_flag(inode, EXT4_INODE_FLAG_HUGE_FILE);
	} else {
		u32int block_count = ext4_sb_get_block_size(sb);
		u32int block_bits =ext4_inode_block_bits_count(block_count);

		ext4_inode_set_flag(inode, EXT4_INODE_FLAG_HUGE_FILE);
		count = count >> (block_bits - 9);
		inode->blocks_count_lo = to_le32((u32int)count);
		inode->osd2.linux2.blocks_high = to_le16((u16int)(count >> 32));
	}

	return 0;
}

u32int ext4_inode_get_flags(struct ext4_inode *inode)
{
	return to_le32(inode->flags);
}
void ext4_inode_set_flags(struct ext4_inode *inode, u32int flags)
{
	inode->flags = to_le32(flags);
}

u32int ext4_inode_get_generation(struct ext4_inode *inode)
{
	return to_le32(inode->generation);
}
void ext4_inode_set_generation(struct ext4_inode *inode, u32int gen)
{
	inode->generation = to_le32(gen);
}

u16int ext4_inode_get_extra_isize(struct ext4_sblock *sb,
				    struct ext4_inode *inode)
{
	u16int inode_size = ext4_get16(sb, inode_size);
	if (inode_size > EXT4_GOOD_OLD_INODE_SIZE)
		return to_le16(inode->extra_isize);
	else
		return 0;
}

void ext4_inode_set_extra_isize(struct ext4_sblock *sb,
				struct ext4_inode *inode,
				u16int size)
{
	u16int inode_size = ext4_get16(sb, inode_size);
	if (inode_size > EXT4_GOOD_OLD_INODE_SIZE)
		inode->extra_isize = to_le16(size);
}

u64int ext4_inode_get_file_acl(struct ext4_inode *inode,
				 struct ext4_sblock *sb)
{
	u64int v = to_le32(inode->file_acl_lo);

	if (ext4_get32(sb, creator_os) == EXT4_SUPERBLOCK_OS_LINUX)
		v |= (u32int)to_le16(inode->osd2.linux2.file_acl_high) << 16;

	return v;
}

void ext4_inode_set_file_acl(struct ext4_inode *inode, struct ext4_sblock *sb,
			     u64int acl)
{
	inode->file_acl_lo = to_le32((acl << 32) >> 32);

	if (ext4_get32(sb, creator_os) == EXT4_SUPERBLOCK_OS_LINUX)
		inode->osd2.linux2.file_acl_high = to_le16((u16int)(acl >> 32));
}

u32int ext4_inode_get_direct_block(struct ext4_inode *inode, u32int idx)
{
	return to_le32(inode->blocks[idx]);
}
void ext4_inode_set_direct_block(struct ext4_inode *inode, u32int idx,
				 u32int block)
{
	inode->blocks[idx] = to_le32(block);
}

u32int ext4_inode_get_indirect_block(struct ext4_inode *inode, u32int idx)
{
	return to_le32(inode->blocks[idx + EXT4_INODE_INDIRECT_BLOCK]);
}

void ext4_inode_set_indirect_block(struct ext4_inode *inode, u32int idx,
				   u32int block)
{
	inode->blocks[idx + EXT4_INODE_INDIRECT_BLOCK] = to_le32(block);
}

u32int ext4_inode_get_dev(struct ext4_inode *inode)
{
	u32int dev_0, dev_1;
	dev_0 = ext4_inode_get_direct_block(inode, 0);
	dev_1 = ext4_inode_get_direct_block(inode, 1);

	if (dev_0)
		return dev_0;
	else
		return dev_1;
}

void ext4_inode_set_dev(struct ext4_inode *inode, u32int dev)
{
	if (dev & ~0xFFFF)
		ext4_inode_set_direct_block(inode, 1, dev);
	else
		ext4_inode_set_direct_block(inode, 0, dev);
}

u32int ext4_inode_type(struct ext4_sblock *sb, struct ext4_inode *inode)
{
	return (ext4_inode_get_mode(sb, inode) & EXT4_INODE_MODE_TYPE_MASK);
}

bool ext4_inode_is_type(struct ext4_sblock *sb, struct ext4_inode *inode,
			u32int type)
{
	return ext4_inode_type(sb, inode) == type;
}

bool ext4_inode_has_flag(struct ext4_inode *inode, u32int f)
{
	return ext4_inode_get_flags(inode) & f;
}

void ext4_inode_clear_flag(struct ext4_inode *inode, u32int f)
{
	u32int flags = ext4_inode_get_flags(inode);
	flags = flags & (~f);
	ext4_inode_set_flags(inode, flags);
}

void ext4_inode_set_flag(struct ext4_inode *inode, u32int f)
{
	u32int flags = ext4_inode_get_flags(inode);
	flags = flags | f;
	ext4_inode_set_flags(inode, flags);
}

bool ext4_inode_can_truncate(struct ext4_sblock *sb, struct ext4_inode *inode)
{
	if ((ext4_inode_has_flag(inode, EXT4_INODE_FLAG_APPEND)) ||
	    (ext4_inode_has_flag(inode, EXT4_INODE_FLAG_IMMUTABLE)))
		return false;

	if ((ext4_inode_is_type(sb, inode, EXT4_INODE_MODE_FILE)) ||
	    (ext4_inode_is_type(sb, inode, EXT4_INODE_MODE_DIRECTORY)) ||
	    (ext4_inode_is_type(sb, inode, EXT4_INODE_MODE_SOFTLINK)))
		return true;

	return false;
}

struct ext4_extent_header *
ext4_inode_get_extent_header(struct ext4_inode *inode)
{
	return (struct ext4_extent_header *)inode->blocks;
}
