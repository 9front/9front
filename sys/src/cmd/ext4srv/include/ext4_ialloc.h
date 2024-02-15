#pragma once

#include "ext4_config.h"
#include "ext4_types.h"

/**@brief Calculate and set checksum of inode bitmap.
 * @param fs fs pointer.
 * @param bg block group
 * @param bitmap bitmap buffer
 */
void ext4_ialloc_set_bitmap_csum(struct ext4_fs *fs, struct ext4_bgroup *bg,
				 void *bitmap);

/**@brief Free i-node number and modify filesystem data structers.
 * @param fs     Filesystem, where the i-node is located
 * @param index  Index of i-node to be release
 * @param is_dir Flag us for information whether i-node is directory or not
 */
int ext4_ialloc_free_inode(struct ext4_fs *fs, u32int index, bool is_dir);

/**@brief I-node allocation algorithm.
 * This is more simple algorithm, than Orlov allocator used
 * in the Linux kernel.
 * @param fs     Filesystem to allocate i-node on
 * @param index  Output value - allocated i-node number
 * @param is_dir Flag if allocated i-node will be file or directory
 * @return Error code
 */
int ext4_ialloc_alloc_inode(struct ext4_fs *fs, u32int *index, bool is_dir);
