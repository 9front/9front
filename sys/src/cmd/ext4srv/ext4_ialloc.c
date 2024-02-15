#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_misc.h"
#include "ext4_debug.h"
#include "ext4_trans.h"
#include "ext4_ialloc.h"
#include "ext4_super.h"
#include "ext4_crc32.h"
#include "ext4_fs.h"
#include "ext4_blockdev.h"
#include "ext4_block_group.h"
#include "ext4_bitmap.h"

/**@brief  Convert i-node number to relative index in block group.
 * @param sb    Superblock
 * @param inode I-node number to be converted
 * @return Index of the i-node in the block group
 */
static u32int ext4_ialloc_inode_to_bgidx(struct ext4_sblock *sb,
					   u32int inode)
{
	u32int inodes_per_group = ext4_get32(sb, inodes_per_group);
	return (inode - 1) % inodes_per_group;
}

/**@brief Convert relative index of i-node to absolute i-node number.
 * @param sb    Superblock
 * @param index Index to be converted
 * @return Absolute number of the i-node
 *
 */
static u32int ext4_ialloc_bgidx_to_inode(struct ext4_sblock *sb,
					   u32int index, u32int bgid)
{
	u32int inodes_per_group = ext4_get32(sb, inodes_per_group);
	return bgid * inodes_per_group + (index + 1);
}

/**@brief Compute block group number from the i-node number.
 * @param sb    Superblock
 * @param inode I-node number to be found the block group for
 * @return Block group number computed from i-node number
 */
static u32int ext4_ialloc_get_bgid_of_inode(struct ext4_sblock *sb,
					      u32int inode)
{
	u32int inodes_per_group = ext4_get32(sb, inodes_per_group);
	return (inode - 1) / inodes_per_group;
}

static u32int ext4_ialloc_bitmap_csum(struct ext4_fs *fs, void *bitmap)
{
	u32int csum = 0;
	if (ext4_sb_feature_ro_com(&fs->sb, EXT4_FRO_COM_METADATA_CSUM)) {
		u32int inodes_per_group =
			ext4_get32(&fs->sb, inodes_per_group);

		/* First calculate crc32 checksum against fs uuid */
		csum = fs->uuid_crc32c;
		/* Then calculate crc32 checksum against inode bitmap */
		csum = ext4_crc32c(csum, bitmap, (inodes_per_group + 7) / 8);
	}
	return csum;
}

void ext4_ialloc_set_bitmap_csum(struct ext4_fs *fs, struct ext4_bgroup *bg, void *bitmap)
{
	int desc_size = ext4_sb_get_desc_size(&fs->sb);
	u32int csum = ext4_ialloc_bitmap_csum(fs, bitmap);
	u16int lo_csum = to_le16(csum & 0xFFFF),
		 hi_csum = to_le16(csum >> 16);

	if (!ext4_sb_feature_ro_com(&fs->sb, EXT4_FRO_COM_METADATA_CSUM))
		return;

	/* See if we need to assign a 32bit checksum */
	bg->inode_bitmap_csum_lo = lo_csum;
	if (desc_size == EXT4_MAX_BLOCK_GROUP_DESCRIPTOR_SIZE)
		bg->inode_bitmap_csum_hi = hi_csum;

}

static bool
ext4_ialloc_verify_bitmap_csum(struct ext4_fs *fs, struct ext4_bgroup *bg, void *bitmap)
{
	int desc_size = ext4_sb_get_desc_size(&fs->sb);
	u32int csum = ext4_ialloc_bitmap_csum(fs, bitmap);
	u16int lo_csum = to_le16(csum & 0xFFFF),
		 hi_csum = to_le16(csum >> 16);

	if (!ext4_sb_feature_ro_com(&fs->sb, EXT4_FRO_COM_METADATA_CSUM))
		return true;

	if (bg->inode_bitmap_csum_lo != lo_csum)
		return false;

	if (desc_size == EXT4_MAX_BLOCK_GROUP_DESCRIPTOR_SIZE)
		if (bg->inode_bitmap_csum_hi != hi_csum)
			return false;

	return true;
}

int ext4_ialloc_free_inode(struct ext4_fs *fs, u32int index, bool is_dir)
{
	struct ext4_sblock *sb = &fs->sb;

	/* Compute index of block group and load it */
	u32int block_group = ext4_ialloc_get_bgid_of_inode(sb, index);

	struct ext4_block_group_ref bg_ref;
	int rc = ext4_fs_get_block_group_ref(fs, block_group, &bg_ref);
	if (rc != 0)
		return rc;

	struct ext4_bgroup *bg = bg_ref.block_group;

	/* Load i-node bitmap */
	ext4_fsblk_t bitmap_block_addr =
	    ext4_bg_get_inode_bitmap(bg, sb);

	struct ext4_block b;
	rc = ext4_trans_block_get(fs->bdev, &b, bitmap_block_addr);
	if (rc != 0)
		return rc;

	if (!ext4_ialloc_verify_bitmap_csum(fs, bg, b.data)) {
		ext4_dbg(DEBUG_IALLOC,
			DBG_WARN "Bitmap checksum failed."
			"Group: %ud\n",
			bg_ref.index);
	}

	/* Free i-node in the bitmap */
	u32int index_in_group = ext4_ialloc_inode_to_bgidx(sb, index);
	ext4_bmap_bit_clr(b.data, index_in_group);
	ext4_ialloc_set_bitmap_csum(fs, bg, b.data);
	ext4_trans_set_block_dirty(b.buf);

	/* Put back the block with bitmap */
	rc = ext4_block_set(fs->bdev, &b);
	if (rc != 0) {
		/* Error in saving bitmap */
		ext4_fs_put_block_group_ref(&bg_ref);
		return rc;
	}

	/* If released i-node is a directory, decrement used directories count
	 */
	if (is_dir) {
		u32int bg_used_dirs = ext4_bg_get_used_dirs_count(bg, sb);
		bg_used_dirs--;
		ext4_bg_set_used_dirs_count(bg, sb, bg_used_dirs);
	}

	/* Update block group free inodes count */
	u32int free_inodes = ext4_bg_get_free_inodes_count(bg, sb);
	free_inodes++;
	ext4_bg_set_free_inodes_count(bg, sb, free_inodes);

	bg_ref.dirty = true;

	/* Put back the modified block group */
	rc = ext4_fs_put_block_group_ref(&bg_ref);
	if (rc != 0)
		return rc;

	/* Update superblock free inodes count */
	ext4_set32(sb, free_inodes_count,
		   ext4_get32(sb, free_inodes_count) + 1);

	return 0;
}

int ext4_ialloc_alloc_inode(struct ext4_fs *fs, u32int *idx, bool is_dir)
{
	struct ext4_sblock *sb = &fs->sb;

	u32int bgid = fs->last_inode_bg_id;
	u32int bg_count = ext4_block_group_cnt(sb);
	u32int sb_free_inodes = ext4_get32(sb, free_inodes_count);
	bool rewind = false;

	/* Try to find free i-node in all block groups */
	while (bgid <= bg_count) {

		if (bgid == bg_count) {
			if (rewind)
				break;
			bg_count = fs->last_inode_bg_id;
			bgid = 0;
			rewind = true;
			continue;
		}

		/* Load block group to check */
		struct ext4_block_group_ref bg_ref;
		int rc = ext4_fs_get_block_group_ref(fs, bgid, &bg_ref);
		if (rc != 0)
			return rc;

		struct ext4_bgroup *bg = bg_ref.block_group;

		/* Read necessary values for algorithm */
		u32int free_inodes = ext4_bg_get_free_inodes_count(bg, sb);
		u32int used_dirs = ext4_bg_get_used_dirs_count(bg, sb);

		/* Check if this block group is good candidate for allocation */
		if (free_inodes > 0) {
			/* Load block with bitmap */
			ext4_fsblk_t bmp_blk_add = ext4_bg_get_inode_bitmap(bg, sb);

			struct ext4_block b;
			rc = ext4_trans_block_get(fs->bdev, &b, bmp_blk_add);
			if (rc != 0) {
				ext4_fs_put_block_group_ref(&bg_ref);
				return rc;
			}

			if (!ext4_ialloc_verify_bitmap_csum(fs, bg, b.data)) {
				ext4_dbg(DEBUG_IALLOC,
					DBG_WARN "Bitmap checksum failed."
					"Group: %ud\n",
					bg_ref.index);
			}

			/* Try to allocate i-node in the bitmap */
			u32int inodes_in_bg;
			u32int idx_in_bg;

			inodes_in_bg = ext4_inodes_in_group_cnt(sb, bgid);
			bool no_space;
			rc = ext4_bmap_bit_find_clr(b.data, 0, inodes_in_bg, &idx_in_bg, &no_space);
			/* Block group does not have any free i-node */
			if (no_space) {
				rc = ext4_block_set(fs->bdev, &b);
				if (rc != 0) {
					ext4_fs_put_block_group_ref(&bg_ref);
					return rc;
				}

				rc = ext4_fs_put_block_group_ref(&bg_ref);
				if (rc != 0)
					return rc;

				continue;
			}

			ext4_bmap_bit_set(b.data, idx_in_bg);

			/* Free i-node found, save the bitmap */
			ext4_ialloc_set_bitmap_csum(fs, bg, b.data);
			ext4_trans_set_block_dirty(b.buf);

			ext4_block_set(fs->bdev, &b);
			if (rc != 0) {
				ext4_fs_put_block_group_ref(&bg_ref);
				return rc;
			}

			/* Modify filesystem counters */
			free_inodes--;
			ext4_bg_set_free_inodes_count(bg, sb, free_inodes);

			/* Increment used directories counter */
			if (is_dir) {
				used_dirs++;
				ext4_bg_set_used_dirs_count(bg, sb, used_dirs);
			}

			/* Decrease unused inodes count */
			u32int unused =
			    ext4_bg_get_itable_unused(bg, sb);

			u32int free = inodes_in_bg - unused;

			if (idx_in_bg >= free) {
				unused = inodes_in_bg - (idx_in_bg + 1);
				ext4_bg_set_itable_unused(bg, sb, unused);
			}

			/* Save modified block group */
			bg_ref.dirty = true;

			rc = ext4_fs_put_block_group_ref(&bg_ref);
			if (rc != 0)
				return rc;

			/* Update superblock */
			sb_free_inodes--;
			ext4_set32(sb, free_inodes_count, sb_free_inodes);

			/* Compute the absolute i-nodex number */
			*idx = ext4_ialloc_bgidx_to_inode(sb, idx_in_bg, bgid);

			fs->last_inode_bg_id = bgid;

			return 0;
		}

		/* Block group not modified, put it and jump to the next block
		 * group */
		ext4_fs_put_block_group_ref(&bg_ref);
		if (rc != 0)
			return rc;

		++bgid;
	}

	werrstr(Enospc);
	return -1;
}
