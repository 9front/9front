#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_misc.h"
#include "ext4_debug.h"
#include "ext4_trans.h"
#include "ext4_balloc.h"
#include "ext4_super.h"
#include "ext4_crc32.h"
#include "ext4_block_group.h"
#include "ext4_fs.h"
#include "ext4_bitmap.h"
#include "ext4_inode.h"

/**@brief Compute number of block group from block address.
 * @param sb superblock pointer.
 * @param baddr Absolute address of block.
 * @return Block group index
 */
u32int ext4_balloc_get_bgid_of_block(struct ext4_sblock *s,
				       u64int baddr)
{
	if (ext4_get32(s, first_data_block) && baddr)
		baddr--;

	return (u32int)(baddr / ext4_get32(s, blocks_per_group));
}

/**@brief Compute the starting block address of a block group
 * @param sb   superblock pointer.
 * @param bgid block group index
 * @return Block address
 */
u64int ext4_balloc_get_block_of_bgid(struct ext4_sblock *s,
				       u32int bgid)
{
	u64int baddr = 0;
	if (ext4_get32(s, first_data_block))
		baddr++;

	baddr += bgid * ext4_get32(s, blocks_per_group);
	return baddr;
}

static u32int ext4_balloc_bitmap_csum(struct ext4_fs *fs, void *bitmap)
{
	u32int checksum = 0;
	if (ext4_sb_feature_ro_com(&fs->sb, EXT4_FRO_COM_METADATA_CSUM)) {
		u32int blocks_per_group = ext4_get32(&fs->sb, blocks_per_group);

		/* First calculate crc32 checksum against fs uuid */
		checksum = fs->uuid_crc32c;
		/* Then calculate crc32 checksum against block_group_desc */
		checksum = ext4_crc32c(checksum, bitmap, blocks_per_group / 8);
	}
	return checksum;
}

void ext4_balloc_set_bitmap_csum(struct ext4_fs *fs,
				struct ext4_bgroup *bg,
				void *bitmap)
{
	int desc_size = ext4_sb_get_desc_size(&fs->sb);
	u32int checksum = ext4_balloc_bitmap_csum(fs, bitmap);
	u16int lo_checksum = to_le16(checksum & 0xFFFF),
		 hi_checksum = to_le16(checksum >> 16);

	if (!ext4_sb_feature_ro_com(&fs->sb, EXT4_FRO_COM_METADATA_CSUM))
		return;

	/* See if we need to assign a 32bit checksum */
	bg->block_bitmap_csum_lo = lo_checksum;
	if (desc_size == EXT4_MAX_BLOCK_GROUP_DESCRIPTOR_SIZE)
		bg->block_bitmap_csum_hi = hi_checksum;

}

static bool
ext4_balloc_verify_bitmap_csum(struct ext4_fs *fs,
					struct ext4_bgroup *bg,
					void *bitmap)
{
	struct ext4_sblock *sb = &fs->sb;
	int desc_size = ext4_sb_get_desc_size(sb);
	u32int checksum = ext4_balloc_bitmap_csum(fs, bitmap);
	u16int lo_checksum = to_le16(checksum & 0xFFFF),
		 hi_checksum = to_le16(checksum >> 16);

	if (!ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM))
		return true;

	if (bg->block_bitmap_csum_lo != lo_checksum)
		return false;

	if (desc_size == EXT4_MAX_BLOCK_GROUP_DESCRIPTOR_SIZE)
		if (bg->block_bitmap_csum_hi != hi_checksum)
			return false;

	return true;
}

int ext4_balloc_free_block(struct ext4_inode_ref *inode_ref, ext4_fsblk_t baddr)
{
	struct ext4_fs *fs = inode_ref->fs;
	struct ext4_sblock *sb = &fs->sb;

	u32int bg_id = ext4_balloc_get_bgid_of_block(sb, baddr);
	u32int index_in_group = ext4_fs_addr_to_idx_bg(sb, baddr);

	/* Load block group reference */
	struct ext4_block_group_ref bg_ref;
	int rc = ext4_fs_get_block_group_ref(fs, bg_id, &bg_ref);
	if (rc != 0)
		return rc;

	struct ext4_bgroup *bg = bg_ref.block_group;

	/* Load block with bitmap */
	ext4_fsblk_t bitmap_block_addr =
	    ext4_bg_get_block_bitmap(bg, sb);

	struct ext4_block bitmap_block;

	rc = ext4_trans_block_get(fs->bdev, &bitmap_block, bitmap_block_addr);
	if (rc != 0) {
		ext4_fs_put_block_group_ref(&bg_ref);
		return rc;
	}

	if (!ext4_balloc_verify_bitmap_csum(fs, bg, bitmap_block.data)) {
		ext4_dbg(DEBUG_BALLOC,
			DBG_WARN "Bitmap checksum failed."
			"Group: %ud\n",
			bg_ref.index);
	}

	/* Modify bitmap */
	ext4_bmap_bit_clr(bitmap_block.data, index_in_group);
	ext4_balloc_set_bitmap_csum(fs, bg, bitmap_block.data);
	ext4_trans_set_block_dirty(bitmap_block.buf);

	/* Release block with bitmap */
	rc = ext4_block_set(fs->bdev, &bitmap_block);
	if (rc != 0) {
		/* Error in saving bitmap */
		ext4_fs_put_block_group_ref(&bg_ref);
		return rc;
	}

	u32int block_size = ext4_sb_get_block_size(sb);

	/* Update superblock free blocks count */
	u64int sb_free_blocks = ext4_sb_get_free_blocks_cnt(sb);
	sb_free_blocks++;
	ext4_sb_set_free_blocks_cnt(sb, sb_free_blocks);

	/* Update inode blocks count */
	u64int ino_blocks = ext4_inode_get_blocks_count(sb, inode_ref->inode);
	ino_blocks -= block_size / EXT4_INODE_BLOCK_SIZE;
	ext4_inode_set_blocks_count(sb, inode_ref->inode, ino_blocks);
	inode_ref->dirty = true;

	/* Update block group free blocks count */
	u32int free_blocks = ext4_bg_get_free_blocks_count(bg, sb);
	free_blocks++;
	ext4_bg_set_free_blocks_count(bg, sb, free_blocks);

	bg_ref.dirty = true;

	rc = ext4_trans_try_revoke_block(fs->bdev, baddr);
	if (rc != 0) {
		bg_ref.dirty = false;
		ext4_fs_put_block_group_ref(&bg_ref);
		return rc;
	}
	ext4_bcache_invalidate_lba(fs->bdev->bc, baddr, 1);
	/* Release block group reference */
	rc = ext4_fs_put_block_group_ref(&bg_ref);

	return rc;
}

int ext4_balloc_free_blocks(struct ext4_inode_ref *inode_ref,
			    ext4_fsblk_t first, u32int count)
{
	int rc = 0;
	u32int blk_cnt = count;
	ext4_fsblk_t start_block = first;
	struct ext4_fs *fs = inode_ref->fs;
	struct ext4_sblock *sb = &fs->sb;

	/* Compute indexes */
	u32int bg_first = ext4_balloc_get_bgid_of_block(sb, first);

	/* Compute indexes */
	u32int bg_last = ext4_balloc_get_bgid_of_block(sb, first + count - 1);

	if (!ext4_sb_feature_incom(sb, EXT4_FINCOM_FLEX_BG)) {
		/*It is not possible without flex_bg that blocks are continuous
		 * and and last block belongs to other bg.*/
		if (bg_last != bg_first) {
			ext4_dbg(DEBUG_BALLOC, DBG_WARN "FLEX_BG: disabled & "
				"bg_last: %ud bg_first: %ud\n",
				bg_last, bg_first);
		}
	}

	/* Load block group reference */
	struct ext4_block_group_ref bg_ref;
	while (bg_first <= bg_last) {

		rc = ext4_fs_get_block_group_ref(fs, bg_first, &bg_ref);
		if (rc != 0)
			return rc;

		struct ext4_bgroup *bg = bg_ref.block_group;

		u32int idx_in_bg_first;
		idx_in_bg_first = ext4_fs_addr_to_idx_bg(sb, first);

		/* Load block with bitmap */
		ext4_fsblk_t bitmap_blk = ext4_bg_get_block_bitmap(bg, sb);

		struct ext4_block blk;
		rc = ext4_trans_block_get(fs->bdev, &blk, bitmap_blk);
		if (rc != 0) {
			ext4_fs_put_block_group_ref(&bg_ref);
			return rc;
		}

		if (!ext4_balloc_verify_bitmap_csum(fs, bg, blk.data)) {
			ext4_dbg(DEBUG_BALLOC,
				DBG_WARN "Bitmap checksum failed."
				"Group: %ud\n",
				bg_ref.index);
		}
		u32int free_cnt;
		free_cnt = ext4_sb_get_block_size(sb) * 8 - idx_in_bg_first;

		/*If last block, free only count blocks*/
		free_cnt = count > free_cnt ? free_cnt : count;

		/* Modify bitmap */
		ext4_bmap_bits_free(blk.data, idx_in_bg_first, free_cnt);
		ext4_balloc_set_bitmap_csum(fs, bg, blk.data);
		ext4_trans_set_block_dirty(blk.buf);

		count -= free_cnt;
		first += free_cnt;

		/* Release block with bitmap */
		rc = ext4_block_set(fs->bdev, &blk);
		if (rc != 0) {
			ext4_fs_put_block_group_ref(&bg_ref);
			return rc;
		}

		u32int block_size = ext4_sb_get_block_size(sb);

		/* Update superblock free blocks count */
		u64int sb_free_blocks = ext4_sb_get_free_blocks_cnt(sb);
		sb_free_blocks += free_cnt;
		ext4_sb_set_free_blocks_cnt(sb, sb_free_blocks);

		/* Update inode blocks count */
		u64int ino_blocks;
		ino_blocks = ext4_inode_get_blocks_count(sb, inode_ref->inode);
		ino_blocks -= free_cnt * (block_size / EXT4_INODE_BLOCK_SIZE);
		ext4_inode_set_blocks_count(sb, inode_ref->inode, ino_blocks);
		inode_ref->dirty = true;

		/* Update block group free blocks count */
		u32int free_blocks;
		free_blocks = ext4_bg_get_free_blocks_count(bg, sb);
		free_blocks += free_cnt;
		ext4_bg_set_free_blocks_count(bg, sb, free_blocks);
		bg_ref.dirty = true;

		/* Release block group reference */
		rc = ext4_fs_put_block_group_ref(&bg_ref);
		if (rc != 0)
			break;

		bg_first++;
	}

	u32int i;
	for (i = 0;i < blk_cnt;i++) {
		rc = ext4_trans_try_revoke_block(fs->bdev, start_block + i);
		if (rc != 0)
			return rc;

	}

	ext4_bcache_invalidate_lba(fs->bdev->bc, start_block, blk_cnt);
	/*All blocks should be released*/
	assert(count == 0);

	return rc;
}

int ext4_balloc_alloc_block(struct ext4_inode_ref *inode_ref,
			    ext4_fsblk_t goal,
			    ext4_fsblk_t *fblock)
{
	ext4_fsblk_t alloc;
	ext4_fsblk_t bmp_blk_adr;
	u32int rel_blk_idx = 0;
	u64int free_blocks;
	int r;
	struct ext4_fs *fs = inode_ref->fs;
	struct ext4_sblock *sb = &fs->sb;

	/* Load block group number for goal and relative index */
	u32int bg_id = ext4_balloc_get_bgid_of_block(sb, goal);
	u32int idx_in_bg = ext4_fs_addr_to_idx_bg(sb, goal);

	struct ext4_block b;
	struct ext4_block_group_ref bg_ref;

	/* Load block group reference */
	r = ext4_fs_get_block_group_ref(fs, bg_id, &bg_ref);
	if (r != 0)
		return r;

	struct ext4_bgroup *bg = bg_ref.block_group;

	free_blocks = ext4_bg_get_free_blocks_count(bg_ref.block_group, sb);
	if (free_blocks == 0) {
		/* This group has no free blocks */
		goto goal_failed;
	}

	/* Compute indexes */
	ext4_fsblk_t first_in_bg;
	first_in_bg = ext4_balloc_get_block_of_bgid(sb, bg_ref.index);

	u32int first_in_bg_index;
	first_in_bg_index = ext4_fs_addr_to_idx_bg(sb, first_in_bg);

	if (idx_in_bg < first_in_bg_index)
		idx_in_bg = first_in_bg_index;

	/* Load block with bitmap */
	bmp_blk_adr = ext4_bg_get_block_bitmap(bg_ref.block_group, sb);

	r = ext4_trans_block_get(fs->bdev, &b, bmp_blk_adr);
	if (r != 0) {
		ext4_fs_put_block_group_ref(&bg_ref);
		return r;
	}

	if (!ext4_balloc_verify_bitmap_csum(fs, bg, b.data)) {
		ext4_dbg(DEBUG_BALLOC,
			DBG_WARN "Bitmap checksum failed."
			"Group: %ud\n",
			bg_ref.index);
	}

	/* Check if goal is free */
	if (ext4_bmap_is_bit_clr(b.data, idx_in_bg)) {
		ext4_bmap_bit_set(b.data, idx_in_bg);
		ext4_balloc_set_bitmap_csum(fs, bg_ref.block_group, b.data);
		ext4_trans_set_block_dirty(b.buf);
		r = ext4_block_set(fs->bdev, &b);
		if (r != 0) {
			ext4_fs_put_block_group_ref(&bg_ref);
			return r;
		}

		alloc = ext4_fs_bg_idx_to_addr(sb, idx_in_bg, bg_id);
		goto success;
	}

	u32int blk_in_bg = ext4_blocks_in_group_cnt(sb, bg_id);

	u32int end_idx = (idx_in_bg + 63) & ~63;
	if (end_idx > blk_in_bg)
		end_idx = blk_in_bg;

	/* Try to find free block near to goal */
	u32int tmp_idx;
	for (tmp_idx = idx_in_bg + 1; tmp_idx < end_idx; ++tmp_idx) {
		if (ext4_bmap_is_bit_clr(b.data, tmp_idx)) {
			ext4_bmap_bit_set(b.data, tmp_idx);

			ext4_balloc_set_bitmap_csum(fs, bg, b.data);
			ext4_trans_set_block_dirty(b.buf);
			r = ext4_block_set(fs->bdev, &b);
			if (r != 0) {
				ext4_fs_put_block_group_ref(&bg_ref);
				return r;
			}

			alloc = ext4_fs_bg_idx_to_addr(sb, tmp_idx, bg_id);
			goto success;
		}
	}

	/* Find free bit in bitmap */
	bool no_space;
	r = ext4_bmap_bit_find_clr(b.data, idx_in_bg, blk_in_bg, &rel_blk_idx, &no_space);
	if (r == 0) {
		ext4_bmap_bit_set(b.data, rel_blk_idx);
		ext4_balloc_set_bitmap_csum(fs, bg_ref.block_group, b.data);
		ext4_trans_set_block_dirty(b.buf);
		r = ext4_block_set(fs->bdev, &b);
		if (r != 0) {
			ext4_fs_put_block_group_ref(&bg_ref);
			return r;
		}

		alloc = ext4_fs_bg_idx_to_addr(sb, rel_blk_idx, bg_id);
		goto success;
	}

	/* No free block found yet */
	r = ext4_block_set(fs->bdev, &b);
	if (r != 0) {
		ext4_fs_put_block_group_ref(&bg_ref);
		return r;
	}

goal_failed:

	r = ext4_fs_put_block_group_ref(&bg_ref);
	if (r != 0)
		return r;

	/* Try other block groups */
	u32int block_group_count = ext4_block_group_cnt(sb);
	u32int bgid = (bg_id + 1) % block_group_count;
	u32int count = block_group_count;

	while (count > 0) {
		r = ext4_fs_get_block_group_ref(fs, bgid, &bg_ref);
		if (r != 0)
			return r;

		struct ext4_bgroup *bg = bg_ref.block_group;
		free_blocks = ext4_bg_get_free_blocks_count(bg, sb);
		if (free_blocks == 0) {
			/* This group has no free blocks */
			goto next_group;
		}

		/* Load block with bitmap */
		bmp_blk_adr = ext4_bg_get_block_bitmap(bg, sb);
		r = ext4_trans_block_get(fs->bdev, &b, bmp_blk_adr);
		if (r != 0) {
			ext4_fs_put_block_group_ref(&bg_ref);
			return r;
		}

		if (!ext4_balloc_verify_bitmap_csum(fs, bg, b.data)) {
			ext4_dbg(DEBUG_BALLOC,
				DBG_WARN "Bitmap checksum failed."
				"Group: %ud\n",
				bg_ref.index);
		}

		/* Compute indexes */
		first_in_bg = ext4_balloc_get_block_of_bgid(sb, bgid);
		idx_in_bg = ext4_fs_addr_to_idx_bg(sb, first_in_bg);
		blk_in_bg = ext4_blocks_in_group_cnt(sb, bgid);
		first_in_bg_index = ext4_fs_addr_to_idx_bg(sb, first_in_bg);

		if (idx_in_bg < first_in_bg_index)
			idx_in_bg = first_in_bg_index;

		bool no_space;
		r = ext4_bmap_bit_find_clr(b.data, idx_in_bg, blk_in_bg, &rel_blk_idx, &no_space);
		if (r == 0) {
			ext4_bmap_bit_set(b.data, rel_blk_idx);
			ext4_balloc_set_bitmap_csum(fs, bg, b.data);
			ext4_trans_set_block_dirty(b.buf);
			r = ext4_block_set(fs->bdev, &b);
			if (r != 0) {
				ext4_fs_put_block_group_ref(&bg_ref);
				return r;
			}

			alloc = ext4_fs_bg_idx_to_addr(sb, rel_blk_idx, bgid);
			goto success;
		}

		r = ext4_block_set(fs->bdev, &b);
		if (r != 0) {
			ext4_fs_put_block_group_ref(&bg_ref);
			return r;
		}

	next_group:
		r = ext4_fs_put_block_group_ref(&bg_ref);
		if (r != 0) {
			return r;
		}

		/* Goto next group */
		bgid = (bgid + 1) % block_group_count;
		count--;
	}

	werrstr("no free blocks");
	return -1;

success:
    /* Empty command - because of syntax */
    ;

	u32int block_size = ext4_sb_get_block_size(sb);

	/* Update superblock free blocks count */
	u64int sb_free_blocks = ext4_sb_get_free_blocks_cnt(sb);
	sb_free_blocks--;
	ext4_sb_set_free_blocks_cnt(sb, sb_free_blocks);

	/* Update inode blocks (different block size!) count */
	u64int ino_blocks = ext4_inode_get_blocks_count(sb, inode_ref->inode);
	ino_blocks += block_size / EXT4_INODE_BLOCK_SIZE;
	ext4_inode_set_blocks_count(sb, inode_ref->inode, ino_blocks);
	inode_ref->dirty = true;

	/* Update block group free blocks count */

	u32int fb_cnt = ext4_bg_get_free_blocks_count(bg_ref.block_group, sb);
	fb_cnt--;
	ext4_bg_set_free_blocks_count(bg_ref.block_group, sb, fb_cnt);

	bg_ref.dirty = true;
	r = ext4_fs_put_block_group_ref(&bg_ref);

	*fblock = alloc;
	return r;
}

int ext4_balloc_try_alloc_block(struct ext4_inode_ref *inode_ref,
				ext4_fsblk_t baddr, bool *free)
{
	int rc;

	struct ext4_fs *fs = inode_ref->fs;
	struct ext4_sblock *sb = &fs->sb;

	/* Compute indexes */
	u32int block_group = ext4_balloc_get_bgid_of_block(sb, baddr);
	u32int index_in_group = ext4_fs_addr_to_idx_bg(sb, baddr);

	/* Load block group reference */
	struct ext4_block_group_ref bg_ref;
	rc = ext4_fs_get_block_group_ref(fs, block_group, &bg_ref);
	if (rc != 0)
		return rc;

	/* Load block with bitmap */
	ext4_fsblk_t bmp_blk_addr;
	bmp_blk_addr = ext4_bg_get_block_bitmap(bg_ref.block_group, sb);

	struct ext4_block b;
	rc = ext4_trans_block_get(fs->bdev, &b, bmp_blk_addr);
	if (rc != 0) {
		ext4_fs_put_block_group_ref(&bg_ref);
		return rc;
	}

	if (!ext4_balloc_verify_bitmap_csum(fs, bg_ref.block_group, b.data)) {
		ext4_dbg(DEBUG_BALLOC,
			DBG_WARN "Bitmap checksum failed."
			"Group: %ud\n",
			bg_ref.index);
	}

	/* Check if block is free */
	*free = ext4_bmap_is_bit_clr(b.data, index_in_group);

	/* Allocate block if possible */
	if (*free) {
		ext4_bmap_bit_set(b.data, index_in_group);
		ext4_balloc_set_bitmap_csum(fs, bg_ref.block_group, b.data);
		ext4_trans_set_block_dirty(b.buf);
	}

	/* Release block with bitmap */
	rc = ext4_block_set(fs->bdev, &b);
	if (rc != 0) {
		/* Error in saving bitmap */
		ext4_fs_put_block_group_ref(&bg_ref);
		return rc;
	}

	/* If block is not free, return */
	if (!(*free))
		goto terminate;

	u32int block_size = ext4_sb_get_block_size(sb);

	/* Update superblock free blocks count */
	u64int sb_free_blocks = ext4_sb_get_free_blocks_cnt(sb);
	sb_free_blocks--;
	ext4_sb_set_free_blocks_cnt(sb, sb_free_blocks);

	/* Update inode blocks count */
	u64int ino_blocks = ext4_inode_get_blocks_count(sb, inode_ref->inode);
	ino_blocks += block_size / EXT4_INODE_BLOCK_SIZE;
	ext4_inode_set_blocks_count(sb, inode_ref->inode, ino_blocks);
	inode_ref->dirty = true;

	/* Update block group free blocks count */
	u32int fb_cnt = ext4_bg_get_free_blocks_count(bg_ref.block_group, sb);
	fb_cnt--;
	ext4_bg_set_free_blocks_count(bg_ref.block_group, sb, fb_cnt);

	bg_ref.dirty = true;

terminate:
	return ext4_fs_put_block_group_ref(&bg_ref);
}
