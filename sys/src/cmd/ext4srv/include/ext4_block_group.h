#pragma once

#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_super.h"

/**@brief Get address of block with data block bitmap.
 * @param bg pointer to block group
 * @param s pointer to superblock
 * @return Address of block with block bitmap
 */
static inline u64int ext4_bg_get_block_bitmap(struct ext4_bgroup *bg,
						struct ext4_sblock *s)
{
	u64int v = to_le32(bg->block_bitmap_lo);

	if (ext4_sb_get_desc_size(s) > EXT4_MIN_BLOCK_GROUP_DESCRIPTOR_SIZE)
		v |= (u64int)to_le32(bg->block_bitmap_hi) << 32;

	return v;
}

/**@brief Set address of block with data block bitmap.
 * @param bg pointer to block group
 * @param s pointer to superblock
 * @param blk block to set
 * @return Address of block with block bitmap
 */
static inline void ext4_bg_set_block_bitmap(struct ext4_bgroup *bg,
					    struct ext4_sblock *s, u64int blk)
{

	bg->block_bitmap_lo = to_le32((u32int)blk);
	if (ext4_sb_get_desc_size(s) > EXT4_MIN_BLOCK_GROUP_DESCRIPTOR_SIZE)
		bg->block_bitmap_hi = to_le32(blk >> 32);

}

/**@brief Get address of block with i-node bitmap.
 * @param bg Pointer to block group
 * @param s Pointer to superblock
 * @return Address of block with i-node bitmap
 */
static inline u64int ext4_bg_get_inode_bitmap(struct ext4_bgroup *bg,
						struct ext4_sblock *s)
{

	u64int v = to_le32(bg->inode_bitmap_lo);

	if (ext4_sb_get_desc_size(s) > EXT4_MIN_BLOCK_GROUP_DESCRIPTOR_SIZE)
		v |= (u64int)to_le32(bg->inode_bitmap_hi) << 32;

	return v;
}

/**@brief Set address of block with i-node bitmap.
 * @param bg Pointer to block group
 * @param s Pointer to superblock
 * @param blk block to set
 * @return Address of block with i-node bitmap
 */
static inline void ext4_bg_set_inode_bitmap(struct ext4_bgroup *bg,
					    struct ext4_sblock *s, u64int blk)
{
	bg->inode_bitmap_lo = to_le32((u32int)blk);
	if (ext4_sb_get_desc_size(s) > EXT4_MIN_BLOCK_GROUP_DESCRIPTOR_SIZE)
		bg->inode_bitmap_hi = to_le32(blk >> 32);

}


/**@brief Get address of the first block of the i-node table.
 * @param bg Pointer to block group
 * @param s Pointer to superblock
 * @return Address of first block of i-node table
 */
static inline u64int
ext4_bg_get_inode_table_first_block(struct ext4_bgroup *bg,
				    struct ext4_sblock *s)
{
	u64int v = to_le32(bg->inode_table_first_block_lo);

	if (ext4_sb_get_desc_size(s) > EXT4_MIN_BLOCK_GROUP_DESCRIPTOR_SIZE)
		v |= (u64int)to_le32(bg->inode_table_first_block_hi) << 32;

	return v;
}

/**@brief Set address of the first block of the i-node table.
 * @param bg Pointer to block group
 * @param s Pointer to superblock
 * @param blk block to set
 * @return Address of first block of i-node table
 */
static inline void
ext4_bg_set_inode_table_first_block(struct ext4_bgroup *bg,
				    struct ext4_sblock *s, u64int blk)
{
	bg->inode_table_first_block_lo = to_le32((u32int)blk);
	if (ext4_sb_get_desc_size(s) > EXT4_MIN_BLOCK_GROUP_DESCRIPTOR_SIZE)
		bg->inode_table_first_block_hi = to_le32(blk >> 32);
}

/**@brief Get number of free blocks in block group.
 * @param bg Pointer to block group
 * @param sb Pointer to superblock
 * @return Number of free blocks in block group
 */
static inline u32int ext4_bg_get_free_blocks_count(struct ext4_bgroup *bg,
						     struct ext4_sblock *s)
{
	u32int v = to_le16(bg->free_blocks_count_lo);

	if (ext4_sb_get_desc_size(s) > EXT4_MIN_BLOCK_GROUP_DESCRIPTOR_SIZE)
		v |= (u32int)to_le16(bg->free_blocks_count_hi) << 16;

	return v;
}

/**@brief Set number of free blocks in block group.
 * @param bg Pointer to block group
 * @param s Pointer to superblock
 * @param cnt Number of free blocks in block group
 */
static inline void ext4_bg_set_free_blocks_count(struct ext4_bgroup *bg,
						 struct ext4_sblock *s,
						 u32int cnt)
{
	bg->free_blocks_count_lo = to_le16((cnt << 16) >> 16);
	if (ext4_sb_get_desc_size(s) > EXT4_MIN_BLOCK_GROUP_DESCRIPTOR_SIZE)
		bg->free_blocks_count_hi = to_le16(cnt >> 16);
}

/**@brief Get number of free i-nodes in block group.
 * @param bg Pointer to block group
 * @param s Pointer to superblock
 * @return Number of free i-nodes in block group
 */
static inline u32int ext4_bg_get_free_inodes_count(struct ext4_bgroup *bg,
						     struct ext4_sblock *s)
{
	u32int v = to_le16(bg->free_inodes_count_lo);

	if (ext4_sb_get_desc_size(s) > EXT4_MIN_BLOCK_GROUP_DESCRIPTOR_SIZE)
		v |= (u32int)to_le16(bg->free_inodes_count_hi) << 16;

	return v;
}

/**@brief Set number of free i-nodes in block group.
 * @param bg Pointer to block group
 * @param s Pointer to superblock
 * @param cnt Number of free i-nodes in block group
 */
static inline void ext4_bg_set_free_inodes_count(struct ext4_bgroup *bg,
						 struct ext4_sblock *s,
						 u32int cnt)
{
	bg->free_inodes_count_lo = to_le16((cnt << 16) >> 16);
	if (ext4_sb_get_desc_size(s) > EXT4_MIN_BLOCK_GROUP_DESCRIPTOR_SIZE)
		bg->free_inodes_count_hi = to_le16(cnt >> 16);
}

/**@brief Get number of used directories in block group.
 * @param bg Pointer to block group
 * @param s Pointer to superblock
 * @return Number of used directories in block group
 */
static inline u32int ext4_bg_get_used_dirs_count(struct ext4_bgroup *bg,
						   struct ext4_sblock *s)
{
	u32int v = to_le16(bg->used_dirs_count_lo);

	if (ext4_sb_get_desc_size(s) > EXT4_MIN_BLOCK_GROUP_DESCRIPTOR_SIZE)
		v |= (u32int)to_le16(bg->used_dirs_count_hi) << 16;

	return v;
}

/**@brief Set number of used directories in block group.
 * @param bg Pointer to block group
 * @param s Pointer to superblock
 * @param cnt Number of used directories in block group
 */
static inline void ext4_bg_set_used_dirs_count(struct ext4_bgroup *bg,
					       struct ext4_sblock *s,
					       u32int cnt)
{
	bg->used_dirs_count_lo = to_le16((cnt << 16) >> 16);
	if (ext4_sb_get_desc_size(s) > EXT4_MIN_BLOCK_GROUP_DESCRIPTOR_SIZE)
		bg->used_dirs_count_hi = to_le16(cnt >> 16);
}

/**@brief Get number of unused i-nodes.
 * @param bg Pointer to block group
 * @param s Pointer to superblock
 * @return Number of unused i-nodes
 */
static inline u32int ext4_bg_get_itable_unused(struct ext4_bgroup *bg,
						 struct ext4_sblock *s)
{

	u32int v = to_le16(bg->itable_unused_lo);

	if (ext4_sb_get_desc_size(s) > EXT4_MIN_BLOCK_GROUP_DESCRIPTOR_SIZE)
		v |= (u32int)to_le16(bg->itable_unused_hi) << 16;

	return v;
}

/**@brief Set number of unused i-nodes.
 * @param bg Pointer to block group
 * @param s Pointer to superblock
 * @param cnt Number of unused i-nodes
 */
static inline void ext4_bg_set_itable_unused(struct ext4_bgroup *bg,
					     struct ext4_sblock *s,
					     u32int cnt)
{
	bg->itable_unused_lo = to_le16((cnt << 16) >> 16);
	if (ext4_sb_get_desc_size(s) > EXT4_MIN_BLOCK_GROUP_DESCRIPTOR_SIZE)
		bg->itable_unused_hi = to_le16(cnt >> 16);
}

/**@brief  Set checksum of block group.
 * @param bg Pointer to block group
 * @param crc Cheksum of block group
 */
static inline void ext4_bg_set_checksum(struct ext4_bgroup *bg, u16int crc)
{
	bg->checksum = to_le16(crc);
}

/**@brief Check if block group has a flag.
 * @param bg Pointer to block group
 * @param flag Flag to be checked
 * @return True if flag is set to 1
 */
static inline bool ext4_bg_has_flag(struct ext4_bgroup *bg, u32int f)
{
	return to_le16(bg->flags) & f;
}

/**@brief Set flag of block group.
 * @param bg Pointer to block group
 * @param flag Flag to be set
 */
static inline void ext4_bg_set_flag(struct ext4_bgroup *bg, u32int f)
{
	u16int flags = to_le16(bg->flags);
	flags |= f;
	bg->flags = to_le16(flags);
}

/**@brief Clear flag of block group.
 * @param bg Pointer to block group
 * @param flag Flag to be cleared
 */
static inline void ext4_bg_clear_flag(struct ext4_bgroup *bg, u32int f)
{
	u16int flags = to_le16(bg->flags);
	flags &= ~f;
	bg->flags = to_le16(flags);
}

/**@brief Calculate CRC16 of the block group.
 * @param crc Init value
 * @param buffer Input buffer
 * @param len Sizeof input buffer
 * @return Computed CRC16*/
u16int ext4_bg_crc16(u16int crc, const u8int *buffer, usize len);
