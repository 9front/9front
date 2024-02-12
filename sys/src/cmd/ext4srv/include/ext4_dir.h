#pragma once

#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_misc.h"
#include "ext4_blockdev.h"
#include "ext4_super.h"

struct ext4_dir_iter {
	struct ext4_inode_ref *inode_ref;
	struct ext4_block curr_blk;
	u64int curr_off;
	struct ext4_dir_en *curr;
};

struct ext4_dir_search_result {
	struct ext4_block block;
	struct ext4_dir_en *dentry;
};


/**@brief Get i-node number from directory entry.
 * @param de Directory entry
 * @return I-node number
 */
static inline u32int
ext4_dir_en_get_inode(struct ext4_dir_en *de)
{
	return to_le32(de->inode);
}

/**@brief Set i-node number to directory entry.
 * @param de Directory entry
 * @param inode I-node number
 */
static inline void
ext4_dir_en_set_inode(struct ext4_dir_en *de, u32int inode)
{
	de->inode = to_le32(inode);
}

/**@brief Set i-node number to directory entry. (For HTree root)
 * @param de Directory entry
 * @param inode I-node number
 */
static inline void
ext4_dx_dot_en_set_inode(struct ext4_dir_idx_dot_en *de, u32int inode)
{
	de->inode = to_le32(inode);
}

/**@brief Get directory entry length.
 * @param de Directory entry
 * @return Entry length
 */
static inline u16int ext4_dir_en_get_entry_len(struct ext4_dir_en *de)
{
	return to_le16(de->entry_len);
}

/**@brief Set directory entry length.
 * @param de     Directory entry
 * @param length Entry length
 */
static inline void ext4_dir_en_set_entry_len(struct ext4_dir_en *de, u16int l)
{
	de->entry_len = to_le16(l);
}

/**@brief Get directory entry name length.
 * @param sb Superblock
 * @param de Directory entry
 * @return Entry name length
 */
static inline u16int ext4_dir_en_get_name_len(struct ext4_sblock *sb,
						struct ext4_dir_en *de)
{
	u16int v = de->name_len;

	if ((ext4_get32(sb, rev_level) == 0) &&
	    (ext4_get32(sb, minor_rev_level) < 5))
		v |= ((u16int)de->in.name_length_high) << 8;

	return v;
}

/**@brief Set directory entry name length.
 * @param sb     Superblock
 * @param de     Directory entry
 * @param length Entry name length
 */
static inline void ext4_dir_en_set_name_len(struct ext4_sblock *sb,
					    struct ext4_dir_en *de,
					    u16int len)
{
	de->name_len = (len << 8) >> 8;

	if ((ext4_get32(sb, rev_level) == 0) &&
	    (ext4_get32(sb, minor_rev_level) < 5))
		de->in.name_length_high = len >> 8;
}

/**@brief Get i-node type of directory entry.
 * @param sb Superblock
 * @param de Directory entry
 * @return I-node type (file, dir, etc.)
 */
static inline u8int ext4_dir_en_get_inode_type(struct ext4_sblock *sb,
						 struct ext4_dir_en *de)
{
	if ((ext4_get32(sb, rev_level) > 0) ||
	    (ext4_get32(sb, minor_rev_level) >= 5))
		return de->in.inode_type;

	return EXT4_DE_UNKNOWN;
}
/**@brief Set i-node type of directory entry.
 * @param sb   Superblock
 * @param de   Directory entry
 * @param type I-node type (file, dir, etc.)
 */

static inline void ext4_dir_en_set_inode_type(struct ext4_sblock *sb,
					      struct ext4_dir_en *de, u8int t)
{
	if ((ext4_get32(sb, rev_level) > 0) ||
	    (ext4_get32(sb, minor_rev_level) >= 5))
		de->in.inode_type = t;
}

/**@brief Verify checksum of a linear directory leaf block
 * @param inode_ref Directory i-node
 * @param dirent    Linear directory leaf block
 * @return true means the block passed checksum verification
 */
bool ext4_dir_csum_verify(struct ext4_inode_ref *inode_ref,
			  struct ext4_dir_en *dirent);

/**@brief Initialize directory iterator.
 * Set position to the first valid entry from the required position.
 * @param it        Pointer to iterator to be initialized
 * @param inode_ref Directory i-node
 * @param pos       Position to start reading entries from
 * @return Error code
 */
int ext4_dir_iterator_init(struct ext4_dir_iter *it,
			   struct ext4_inode_ref *inode_ref, u64int pos);

/**@brief Jump to the next valid entry
 * @param it Initialized iterator
 * @return Error code
 */
int ext4_dir_iterator_next(struct ext4_dir_iter *it);

/**@brief Uninitialize directory iterator.
 *        Release all allocated structures.
 * @param it Iterator to be finished
 * @return Error code
 */
int ext4_dir_iterator_fini(struct ext4_dir_iter *it);

/**@brief Write directory entry to concrete data block.
 * @param sb        Superblock
 * @param en     Pointer to entry to be written
 * @param entry_len Length of new entry
 * @param child     Child i-node to be written to new entry
 * @param name      Name of the new entry
 * @param name_len  Length of entry name
 */
void ext4_dir_write_entry(struct ext4_sblock *sb, struct ext4_dir_en *en,
			  u16int entry_len, struct ext4_inode_ref *child,
			  const char *name, usize name_len);

/**@brief Add new entry to the directory.
 * @param parent Directory i-node
 * @param name   Name of new entry
 * @param child  I-node to be referenced from new entry
 * @return Error code
 */
int ext4_dir_add_entry(struct ext4_inode_ref *parent, const char *name,
		       u32int name_len, struct ext4_inode_ref *child);

/**@brief Find directory entry with passed name.
 * @param result Result structure to be returned if entry found
 * @param parent Directory i-node
 * @param name   Name of entry to be found
 * @param name_len  Name length
 * @return Error code
 */
int ext4_dir_find_entry(struct ext4_dir_search_result *result,
			struct ext4_inode_ref *parent, const char *name,
			u32int name_len);

/**@brief Remove directory entry.
 * @param parent Directory i-node
 * @param name   Name of the entry to be removed
 * @param name_len  Name length
 * @return Error code
 */
int ext4_dir_remove_entry(struct ext4_inode_ref *parent, const char *name,
			  u32int name_len);

/**@brief Try to insert entry to concrete data block.
 * @param sb           Superblock
 * @param inode_ref    Directory i-node
 * @param dst_blk      Block to try to insert entry to
 * @param child        Child i-node to be inserted by new entry
 * @param name         Name of the new entry
 * @param name_len     Length of the new entry name
 * @return Error code
 */
int ext4_dir_try_insert_entry(struct ext4_sblock *sb,
			      struct ext4_inode_ref *inode_ref,
			      struct ext4_block *dst_blk,
			      struct ext4_inode_ref *child, const char *name,
			      u32int name_len);

/**@brief Try to find entry in block by name.
 * @param block     Block containing entries
 * @param sb        Superblock
 * @param name_len  Length of entry name
 * @param name      Name of entry to be found
 * @param res_entry Output pointer to found entry, nil if not found
 * @return Error code
 */
int ext4_dir_find_in_block(struct ext4_block *block, struct ext4_sblock *sb,
			   usize name_len, const char *name,
			   struct ext4_dir_en **res_entry);

/**@brief Simple function to release allocated data from result.
 * @param parent Parent inode
 * @param result Search result to destroy
 * @return Error code
 *
 */
int ext4_dir_destroy_result(struct ext4_inode_ref *parent,
			    struct ext4_dir_search_result *result);

void ext4_dir_set_csum(struct ext4_inode_ref *inode_ref,
		       struct ext4_dir_en *dirent);


void ext4_dir_init_entry_tail(struct ext4_dir_entry_tail *t);
