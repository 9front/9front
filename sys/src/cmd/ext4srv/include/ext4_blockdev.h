#pragma once

#include "ext4_bcache.h"

struct ext4_blockdev_iface {
	/**@brief   Open device function
	 * @param   bdev block device.*/
	int (*open)(struct ext4_blockdev *bdev);

	/**@brief   Block read function.
	 * @param   bdev block device
	 * @param   buf output buffer
	 * @param   blk_id block id
	 * @param   blk_cnt block count*/
	int (*bread)(struct ext4_blockdev *bdev, void *buf, u64int blk_id,
		     u32int blk_cnt);

	/**@brief   Block write function.
	 * @param   buf input buffer
	 * @param   blk_id block id
	 * @param   blk_cnt block count*/
	int (*bwrite)(struct ext4_blockdev *bdev, const void *buf,
		      u64int blk_id, u32int blk_cnt);

	/**@brief   Close device function.
	 * @param   bdev block device.*/
	int (*close)(struct ext4_blockdev *bdev);

	/**@brief   Lock block device. Required in multi partition mode
	 *          operations. Not mandatory field.
	 * @param   bdev block device.*/
	int (*lock)(struct ext4_blockdev *bdev);

	/**@brief   Unlock block device. Required in multi partition mode
	 *          operations. Not mandatory field.
	 * @param   bdev block device.*/
	int (*unlock)(struct ext4_blockdev *bdev);

	/**@brief   Block size (bytes): physical*/
	u32int ph_bsize;

	/**@brief   Block count: physical*/
	u64int ph_bcnt;

	/**@brief   Block size buffer: physical*/
	u8int *ph_bbuf;

	/**@brief   Reference counter to block device interface*/
	u32int ph_refctr;

	/**@brief   Physical read counter*/
	u32int bread_ctr;

	/**@brief   Physical write counter*/
	u32int bwrite_ctr;

	/**@brief   User data pointer*/
	void* p_user;
};

/**@brief   Definition of the simple block device.*/
struct ext4_blockdev {
	/**@brief Block device interface*/
	struct ext4_blockdev_iface *bdif;

	/**@brief Offset in bdif. For multi partition mode.*/
	u64int part_offset;

	/**@brief Part size in bdif. For multi partition mode.*/
	u64int part_size;

	/**@brief   Block cache.*/
	struct ext4_bcache *bc;

	/**@brief   Block size (bytes) logical*/
	u32int lg_bsize;

	/**@brief   Block count: logical*/
	u64int lg_bcnt;

	/**@brief   Cache write back mode reference counter*/
	u32int cache_write_back;

	/**@brief   The filesystem this block device belongs to. */
	struct ext4_fs *fs;

	void *journal;
};

#pragma incomplete struct ext4_blockdev

/**@brief   Static initialization of the block device.*/
#define EXT4_BLOCKDEV_STATIC_INSTANCE(__name, __bsize, __bcnt, __open, __bread,\
				      __bwrite, __close, __lock, __unlock)     \
	static u8int __name##_ph_bbuf[(__bsize)];                            \
	static struct ext4_blockdev_iface __name##_iface = {                   \
		.open = __open,                                                \
		.bread = __bread,                                              \
		.bwrite = __bwrite,                                            \
		.close = __close,                                              \
		.lock = __lock,                                                \
		.unlock = __unlock,                                            \
		.ph_bsize = __bsize,                                           \
		.ph_bcnt = __bcnt,                                             \
		.ph_bbuf = __name##_ph_bbuf,                                   \
	};								       \
	static struct ext4_blockdev __name = {                                 \
		.bdif = &__name##_iface,                                       \
		.part_offset = 0,                                              \
		.part_size =  (__bcnt) * (__bsize),                            \
	}

/**@brief   Block device initialization.
 * @param   bdev block device descriptor
 * @return  standard error code*/
int ext4_block_init(struct ext4_blockdev *bdev);

/**@brief   Binds a bcache to block device.
 * @param   bdev block device descriptor
 * @param   bc block cache descriptor
 * @return  standard error code*/
int ext4_block_bind_bcache(struct ext4_blockdev *bdev, struct ext4_bcache *bc);

/**@brief   Close block device
 * @param   bdev block device descriptor
 * @return  standard error code*/
int ext4_block_fini(struct ext4_blockdev *bdev);

/**@brief   Flush data in given buffer to disk.
 * @param   bdev block device descriptor
 * @param   buf buffer
 * @return  standard error code*/
int ext4_block_flush_buf(struct ext4_blockdev *bdev, struct ext4_buf *buf);

/**@brief   Flush data in buffer of given lba to disk,
 *          if that buffer exists in block cache.
 * @param   bdev block device descriptor
 * @param   lba logical block address
 * @return  standard error code*/
int ext4_block_flush_lba(struct ext4_blockdev *bdev, u64int lba);

/**@brief   Set logical block size in block device.
 * @param   bdev block device descriptor
 * @param   lb_size logical block size (in bytes)
 * @return  standard error code*/
void ext4_block_set_lb_size(struct ext4_blockdev *bdev, u32int lb_bsize);

/**@brief   Block get function (through cache, don't read).
 * @param   bdev block device descriptor
 * @param   b block descriptor
 * @param   lba logical block address
 * @return  standard error code*/
int ext4_block_get_noread(struct ext4_blockdev *bdev, struct ext4_block *b,
			  u64int lba);

/**@brief   Block get function (through cache).
 * @param   bdev block device descriptor
 * @param   b block descriptor
 * @param   lba logical block address
 * @return  standard error code*/
int ext4_block_get(struct ext4_blockdev *bdev, struct ext4_block *b,
		   u64int lba);

/**@brief   Block set procedure (through cache).
 * @param   bdev block device descriptor
 * @param   b block descriptor
 * @return  standard error code*/
int ext4_block_set(struct ext4_blockdev *bdev, struct ext4_block *b);

/**@brief   Block read procedure (without cache)
 * @param   bdev block device descriptor
 * @param   buf output buffer
 * @param   lba logical block address
 * @return  standard error code*/
int ext4_blocks_get_direct(struct ext4_blockdev *bdev, void *buf, u64int lba,
			   u32int cnt);

/**@brief   Block write procedure (without cache)
 * @param   bdev block device descriptor
 * @param   buf output buffer
 * @param   lba logical block address
 * @return  standard error code*/
int ext4_blocks_set_direct(struct ext4_blockdev *bdev, const void *buf,
			   u64int lba, u32int cnt);

/**@brief   Write to block device (by direct address).
 * @param   bdev block device descriptor
 * @param   off byte offset in block device
 * @param   buf input buffer
 * @param   len length of the write buffer
 * @return  standard error code*/
int ext4_block_writebytes(struct ext4_blockdev *bdev, u64int off,
			  const void *buf, u32int len);

/**@brief   Read freom block device (by direct address).
 * @param   bdev block device descriptor
 * @param   off byte offset in block device
 * @param   buf input buffer
 * @param   len length of the write buffer
 * @return  standard error code*/
int ext4_block_readbytes(struct ext4_blockdev *bdev, u64int off, void *buf,
			 u32int len);

/**@brief   Flush all dirty buffers to disk
 * @param   bdev block device descriptor
 * @return  standard error code*/
int ext4_block_cache_flush(struct ext4_blockdev *bdev);

/**@brief   Enable/disable write back cache mode
 * @param   bdev block device descriptor
 * @param   on_off
 *              !0 - ENABLE
 *               0 - DISABLE (all delayed cache buffers will be flushed)
 * @return  standard error code*/
int ext4_block_cache_write_back(struct ext4_blockdev *bdev, u8int on_off);
