#pragma once

#include "ext4_config.h"
#include "ext4_types.h"

/**@brief   Mark a buffer dirty and add it to the current transaction.
 * @param   buf buffer
 * @return  standard error code*/
int ext4_trans_set_block_dirty(struct ext4_buf *buf);

/**@brief   Block get function (through cache, don't read).
 *          jbd_trans_get_access would be called in order to
 *          get write access to the buffer.
 * @param   bdev block device descriptor
 * @param   b block descriptor
 * @param   lba logical block address
 * @return  standard error code*/
int ext4_trans_block_get_noread(struct ext4_blockdev *bdev,
			  struct ext4_block *b,
			  u64int lba);

/**@brief   Block get function (through cache).
 *          jbd_trans_get_access would be called in order to
 *          get write access to the buffer.
 * @param   bdev block device descriptor
 * @param   b block descriptor
 * @param   lba logical block address
 * @return  standard error code*/
int ext4_trans_block_get(struct ext4_blockdev *bdev,
		   struct ext4_block *b,
		   u64int lba);

/**@brief  Try to add block to be revoked to the current transaction.
 * @param  bdev block device descriptor
 * @param  lba logical block address
 * @return standard error code*/
int ext4_trans_try_revoke_block(struct ext4_blockdev *bdev,
			       u64int lba);
