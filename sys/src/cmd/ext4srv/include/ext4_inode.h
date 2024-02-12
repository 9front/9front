#pragma once

#include "ext4_config.h"
#include "ext4_types.h"

#pragma incomplete struct ext4_extent_header

/**@brief Get mode of the i-node.
 * @param sb    Superblock
 * @param inode I-node to load mode from
 * @return Mode of the i-node
 */
u32int ext4_inode_get_mode(struct ext4_sblock *sb, struct ext4_inode *inode);

/**@brief Set mode of the i-node.
 * @param sb    Superblock
 * @param inode I-node to set mode to
 * @param mode  Mode to set to i-node
 */
void ext4_inode_set_mode(struct ext4_sblock *sb, struct ext4_inode *inode,
			 u32int mode);

/**@brief Get ID of the i-node owner (user id).
 * @param inode I-node to load uid from
 * @return User ID of the i-node owner
 */
u32int ext4_inode_get_uid(struct ext4_inode *inode);

/**@brief Set ID of the i-node owner.
 * @param inode I-node to set uid to
 * @param uid   ID of the i-node owner
 */
void ext4_inode_set_uid(struct ext4_inode *inode, u32int uid);

/**@brief Get real i-node size.
 * @param sb    Superblock
 * @param inode I-node to load size from
 * @return Real size of i-node
 */
u64int ext4_inode_get_size(struct ext4_sblock *sb, struct ext4_inode *inode);

/**@brief Set real i-node size.
 * @param inode I-node to set size to
 * @param size  Size of the i-node
 */
void ext4_inode_set_size(struct ext4_inode *inode, u64int size);

/**@brief Get time, when i-node was last accessed.
 * @param inode I-node
 * @return Time of the last access (POSIX)
 */
u32int ext4_inode_get_access_time(struct ext4_inode *inode);

/**@brief Set time, when i-node was last accessed.
 * @param inode I-node
 * @param time  Time of the last access (POSIX)
 */
void ext4_inode_set_access_time(struct ext4_inode *inode, u32int time);

/**@brief Get time, when i-node was last changed.
 * @param inode I-node
 * @return Time of the last change (POSIX)
 */
u32int ext4_inode_get_change_inode_time(struct ext4_inode *inode);

/**@brief Set time, when i-node was last changed.
 * @param inode I-node
 * @param time  Time of the last change (POSIX)
 */
void ext4_inode_set_change_inode_time(struct ext4_inode *inode, u32int time);

/**@brief Get time, when i-node content was last modified.
 * @param inode I-node
 * @return Time of the last content modification (POSIX)
 */
u32int ext4_inode_get_modif_time(struct ext4_inode *inode);

/**@brief Set time, when i-node content was last modified.
 * @param inode I-node
 * @param time  Time of the last content modification (POSIX)
 */
void ext4_inode_set_modif_time(struct ext4_inode *inode, u32int time);

/**@brief Get time, when i-node was deleted.
 * @param inode I-node
 * @return Time of the delete action (POSIX)
 */
u32int ext4_inode_get_del_time(struct ext4_inode *inode);

/**@brief Get time, when i-node was created.
 * @param inode I-node
 * @return Time of the create action (POSIX)
 */
u32int ext4_inode_get_creation_time(struct ext4_inode *inode);

/**@brief Set time, when i-node was deleted.
 * @param inode I-node
 * @param time  Time of the delete action (POSIX)
 */
void ext4_inode_set_del_time(struct ext4_inode *inode, u32int time);

/**@brief Get ID of the i-node owner's group.
 * @param inode I-node to load gid from
 * @return Group ID of the i-node owner
 */
u32int ext4_inode_get_gid(struct ext4_inode *inode);

/**@brief Set ID to the i-node owner's group.
 * @param inode I-node to set gid to
 * @param gid   Group ID of the i-node owner
 */
void ext4_inode_set_gid(struct ext4_inode *inode, u32int gid);

/**@brief Get number of links to i-node.
 * @param inode I-node to load number of links from
 * @return Number of links to i-node
 */
u16int ext4_inode_get_links_cnt(struct ext4_inode *inode);

/**@brief Set number of links to i-node.
 * @param inode I-node to set number of links to
 * @param count Number of links to i-node
 */
void ext4_inode_set_links_cnt(struct ext4_inode *inode, u16int cnt);

/**@brief Get number of 512-bytes blocks used for i-node.
 * @param sb    Superblock
 * @param inode I-node
 * @return Number of 512-bytes blocks
 */
u64int ext4_inode_get_blocks_count(struct ext4_sblock *sb,
				     struct ext4_inode *inode);

/**@brief Set number of 512-bytes blocks used for i-node.
 * @param sb    Superblock
 * @param inode I-node
 * @param count Number of 512-bytes blocks
 * @return Error code
 */
int ext4_inode_set_blocks_count(struct ext4_sblock *sb,
				struct ext4_inode *inode, u64int cnt);

/**@brief Get flags (features) of i-node.
 * @param inode I-node to get flags from
 * @return Flags (bitmap)
 */
u32int ext4_inode_get_flags(struct ext4_inode *inode);

/**@brief Set flags (features) of i-node.
 * @param inode I-node to set flags to
 * @param flags Flags to set to i-node
 */
void ext4_inode_set_flags(struct ext4_inode *inode, u32int flags);

/**@brief Get file generation (used by NFS).
 * @param inode I-node
 * @return File generation
 */
u32int ext4_inode_get_generation(struct ext4_inode *inode);

/**@brief Set file generation (used by NFS).
 * @param inode      I-node
 * @param generation File generation
 */
void ext4_inode_set_generation(struct ext4_inode *inode, u32int gen);

/**@brief Get extra I-node size field.
 * @param sb         Superblock
 * @param inode      I-node
 * @return extra I-node size
 */
u16int ext4_inode_get_extra_isize(struct ext4_sblock *sb,
				    struct ext4_inode *inode);

/**@brief Set extra I-node size field.
 * @param sb         Superblock
 * @param inode      I-node
 * @param size       extra I-node size
 */
void ext4_inode_set_extra_isize(struct ext4_sblock *sb,
				struct ext4_inode *inode,
				u16int size);

/**@brief Get address of block, where are extended attributes located.
 * @param inode I-node
 * @param sb    Superblock
 * @return Block address
 */
u64int ext4_inode_get_file_acl(struct ext4_inode *inode,
				 struct ext4_sblock *sb);

/**@brief Set address of block, where are extended attributes located.
 * @param inode    I-node
 * @param sb       Superblock
 * @param file_acl Block address
 */
void ext4_inode_set_file_acl(struct ext4_inode *inode, struct ext4_sblock *sb,
			     u64int acl);

/**@brief Get block address of specified direct block.
 * @param inode I-node to load block from
 * @param idx   Index of logical block
 * @return Physical block address
 */
u32int ext4_inode_get_direct_block(struct ext4_inode *inode, u32int idx);

/**@brief Set block address of specified direct block.
 * @param inode  I-node to set block address to
 * @param idx    Index of logical block
 * @param fblock Physical block address
 */
void ext4_inode_set_direct_block(struct ext4_inode *inode, u32int idx,
				 u32int block);

/**@brief Get block address of specified indirect block.
 * @param inode I-node to get block address from
 * @param idx   Index of indirect block
 * @return Physical block address
 */
u32int ext4_inode_get_indirect_block(struct ext4_inode *inode, u32int idx);

/**@brief Set block address of specified indirect block.
 * @param inode  I-node to set block address to
 * @param idx    Index of indirect block
 * @param fblock Physical block address
 */
void ext4_inode_set_indirect_block(struct ext4_inode *inode, u32int idx,
				   u32int block);

/**@brief Get device number
 * @param inode  I-node to get device number from
 * @return Device number
 */
u32int ext4_inode_get_dev(struct ext4_inode *inode);

/**@brief Set device number
 * @param inode  I-node to set device number to
 * @param dev    Device number
 */
void ext4_inode_set_dev(struct ext4_inode *inode, u32int dev);

/**@brief return the type of i-node
 * @param sb    Superblock
 * @param inode I-node to return the type of
 * @return Result of check operation
 */
u32int ext4_inode_type(struct ext4_sblock *sb, struct ext4_inode *inode);

/**@brief Check if i-node has specified type.
 * @param sb    Superblock
 * @param inode I-node to check type of
 * @param type  Type to check
 * @return Result of check operation
 */
bool ext4_inode_is_type(struct ext4_sblock *sb, struct ext4_inode *inode,
			u32int type);

/**@brief Check if i-node has specified flag.
 * @param inode I-node to check flags of
 * @param flag  Flag to check
 * @return Result of check operation
 */
bool ext4_inode_has_flag(struct ext4_inode *inode, u32int f);

/**@brief Remove specified flag from i-node.
 * @param inode      I-node to clear flag on
 * @param clear_flag Flag to be cleared
 */
void ext4_inode_clear_flag(struct ext4_inode *inode, u32int f);

/**@brief Set specified flag to i-node.
 * @param inode    I-node to set flag on
 * @param set_flag Flag to be set
 */
void ext4_inode_set_flag(struct ext4_inode *inode, u32int f);

/**@brief Get inode checksum(crc32)
 * @param sb    Superblock
 * @param inode I-node to get checksum value from
 */
u32int
ext4_inode_get_csum(struct ext4_sblock *sb, struct ext4_inode *inode);

/**@brief Get inode checksum(crc32)
 * @param sb    Superblock
 * @param inode I-node to get checksum value from
 */
void
ext4_inode_set_csum(struct ext4_sblock *sb, struct ext4_inode *inode,
			u32int checksum);

/**@brief Check if i-node can be truncated.
 * @param sb    Superblock
 * @param inode I-node to check
 * @return Result of the check operation
 */
bool ext4_inode_can_truncate(struct ext4_sblock *sb, struct ext4_inode *inode);

/**@brief Get extent header from the root of the extent tree.
 * @param inode I-node to get extent header from
 * @return Pointer to extent header of the root node
 */
struct ext4_extent_header *
ext4_inode_get_extent_header(struct ext4_inode *inode);
