#pragma once

#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_fs.h"

/**@brief Compute number of block group from block address.
 * @param sb superblock pointer.
 * @param baddr Absolute address of block.
 * @return Block group index
 */
u32int ext4_balloc_get_bgid_of_block(struct ext4_sblock *s,
				       ext4_fsblk_t baddr);

/**@brief Compute the starting block address of a block group
 * @param sb   superblock pointer.
 * @param bgid block group index
 * @return Block address
 */
ext4_fsblk_t ext4_balloc_get_block_of_bgid(struct ext4_sblock *s,
					   u32int bgid);

/**@brief Calculate and set checksum of block bitmap.
 * @param sb superblock pointer.
 * @param bg block group
 * @param bitmap bitmap buffer
 */
void ext4_balloc_set_bitmap_csum(struct ext4_fs *fs,
				struct ext4_bgroup *bg,
				void *bitmap);

/**@brief   Free block from inode.
 * @param   inode_ref inode reference
 * @param   baddr block address
 * @return  standard error code*/
int ext4_balloc_free_block(struct ext4_inode_ref *inode_ref,
			   ext4_fsblk_t baddr);

/**@brief   Free blocks from inode.
 * @param   inode_ref inode reference
 * @param   first block address
 * @param   count block count
 * @return  standard error code*/
int ext4_balloc_free_blocks(struct ext4_inode_ref *inode_ref,
			    ext4_fsblk_t first, u32int count);

/**@brief   Allocate block procedure.
 * @param   inode_ref inode reference
 * @param   goal
 * @param   baddr allocated block address
 * @return  standard error code*/
int ext4_balloc_alloc_block(struct ext4_inode_ref *inode_ref,
			    ext4_fsblk_t goal,
			    ext4_fsblk_t *baddr);

/**@brief   Try allocate selected block.
 * @param   inode_ref inode reference
 * @param   baddr block address to allocate
 * @param   free if baddr is not allocated
 * @return  standard error code*/
int ext4_balloc_try_alloc_block(struct ext4_inode_ref *inode_ref,
				ext4_fsblk_t baddr, bool *free);
