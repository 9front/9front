#pragma once

#include "tree.h"
#include "queue.h"

#define EXT4_BLOCK_ZERO() 	\
	{0}

/**@brief   Single block descriptor*/
struct ext4_buf {
	/**@brief   Flags*/
	int flags;

	/**@brief   Logical block address*/
	u64int lba;

	/**@brief   Data buffer.*/
	u8int *data;

	/**@brief   LRU priority. (unused) */
	u32int lru_prio;

	/**@brief   LRU id.*/
	u32int lru_id;

	/**@brief   Reference count table*/
	u32int refctr;

	/**@brief   The block cache this buffer belongs to. */
	struct ext4_bcache *bc;

	/**@brief   Whether or not buffer is on dirty list.*/
	bool on_dirty_list;

	/**@brief   LBA tree node*/
	RB_ENTRY(ext4_buf) lba_node;

	/**@brief   LRU tree node*/
	RB_ENTRY(ext4_buf) lru_node;

	/**@brief   Dirty list node*/
	SLIST_ENTRY(ext4_buf) dirty_node;

	/**@brief   Callback routine after a disk-write operation.
	 * @param   bc block cache descriptor
	 * @param   buf buffer descriptor
	 * @param   standard error code returned by bdev->bwrite()
	 * @param   arg argument passed to this routine*/
	void (*end_write)(struct ext4_bcache *bc,
			  struct ext4_buf *buf,
			  int res,
			  void *arg);

	/**@brief   argument passed to end_write() callback.*/
	void *end_write_arg;
};

/**@brief   Single block descriptor*/
struct ext4_block {
	/**@brief   Logical block ID*/
	u64int lb_id;

	/**@brief   Buffer */
	struct ext4_buf *buf;

	/**@brief   Data buffer.*/
	u8int *data;
};

/**@brief   Block cache descriptor*/
struct ext4_bcache {

	/**@brief   Item count in block cache*/
	u32int cnt;

	/**@brief   Item size in block cache*/
	u32int itemsize;

	/**@brief   Last recently used counter*/
	u32int lru_ctr;

	/**@brief   Currently referenced datablocks*/
	u32int ref_blocks;

	/**@brief   Maximum referenced datablocks*/
	u32int max_ref_blocks;

	/**@brief   The blockdev binded to this block cache*/
	struct ext4_blockdev *bdev;

	/**@brief   The cache should not be shaked */
	bool dont_shake;

	/**@brief   A tree holding all bufs*/
	RB_HEAD(ext4_buf_lba, ext4_buf) lba_root;

	/**@brief   A tree holding unreferenced bufs*/
	RB_HEAD(ext4_buf_lru, ext4_buf) lru_root;

	/**@brief   A singly-linked list holding dirty buffers*/
	SLIST_HEAD(ext4_buf_dirty, ext4_buf) dirty_list;
};

/**@brief buffer state bits
 *
 *  - BC_UPTODATE: Buffer contains valid data.
 *  - BC_DIRTY: Buffer is dirty.
 *  - BC_FLUSH: Buffer will be immediately flushed,
 *              when no one references it.
 *  - BC_TMP: Buffer will be dropped once its refctr
 *            reaches zero.
 */
enum bcache_state_bits {
	BC_UPTODATE,
	BC_DIRTY,
	BC_FLUSH,
	BC_TMP
};

#define ext4_bcache_set_flag(buf, b)    \
	(buf)->flags |= 1 << (b)

#define ext4_bcache_clear_flag(buf, b)    \
	(buf)->flags &= ~(1 << (b))

#define ext4_bcache_test_flag(buf, b)    \
	(((buf)->flags & (1 << (b))) >> (b))

static inline void ext4_bcache_set_dirty(struct ext4_buf *buf) {
	ext4_bcache_set_flag(buf, BC_UPTODATE);
	ext4_bcache_set_flag(buf, BC_DIRTY);
}

static inline void ext4_bcache_clear_dirty(struct ext4_buf *buf) {
	ext4_bcache_clear_flag(buf, BC_UPTODATE);
	ext4_bcache_clear_flag(buf, BC_DIRTY);
}

/**@brief   Increment reference counter of buf by 1.*/
#define ext4_bcache_inc_ref(buf) ((buf)->refctr++)

/**@brief   Decrement reference counter of buf by 1.*/
#define ext4_bcache_dec_ref(buf) ((buf)->refctr--)

/**@brief   Insert buffer to dirty cache list
 * @param   bc block cache descriptor
 * @param   buf buffer descriptor */
static inline void
ext4_bcache_insert_dirty_node(struct ext4_bcache *bc, struct ext4_buf *buf) {
	if (!buf->on_dirty_list) {
		SLIST_INSERT_HEAD(&bc->dirty_list, buf, dirty_node);
		buf->on_dirty_list = true;
	}
}

/**@brief   Remove buffer to dirty cache list
 * @param   bc block cache descriptor
 * @param   buf buffer descriptor */
static inline void
ext4_bcache_remove_dirty_node(struct ext4_bcache *bc, struct ext4_buf *buf) {
	if (buf->on_dirty_list) {
		SLIST_REMOVE(&bc->dirty_list, buf, ext4_buf, dirty_node);
		buf->on_dirty_list = false;
	}
}


/**@brief   Dynamic initialization of block cache.
 * @param   bc block cache descriptor
 * @param   cnt items count in block cache
 * @param   itemsize single item size (in bytes)
 * @return  standard error code*/
int ext4_bcache_init_dynamic(struct ext4_bcache *bc, u32int cnt,
			     u32int itemsize);

/**@brief   Do cleanup works on block cache.
 * @param   bc block cache descriptor.*/
void ext4_bcache_cleanup(struct ext4_bcache *bc);

/**@brief   Dynamic de-initialization of block cache.
 * @param   bc block cache descriptor
 * @return  standard error code*/
int ext4_bcache_fini_dynamic(struct ext4_bcache *bc);

/**@brief   Get a buffer with the lowest LRU counter in bcache.
 * @param   bc block cache descriptor
 * @return  buffer with the lowest LRU counter*/
struct ext4_buf *ext4_buf_lowest_lru(struct ext4_bcache *bc);

/**@brief   Drop unreferenced buffer from bcache.
 * @param   bc block cache descriptor
 * @param   buf buffer*/
void ext4_bcache_drop_buf(struct ext4_bcache *bc, struct ext4_buf *buf);

/**@brief   Invalidate a buffer.
 * @param   bc block cache descriptor
 * @param   buf buffer*/
void ext4_bcache_invalidate_buf(struct ext4_bcache *bc,
				struct ext4_buf *buf);

/**@brief   Invalidate a range of buffers.
 * @param   bc block cache descriptor
 * @param   from starting lba
 * @param   cnt block counts
 * @param   buf buffer*/
void ext4_bcache_invalidate_lba(struct ext4_bcache *bc,
				u64int from,
				u32int cnt);

/**@brief   Find existing buffer from block cache memory.
 *          Unreferenced block allocation is based on LRU
 *          (Last Recently Used) algorithm.
 * @param   bc block cache descriptor
 * @param   b block to alloc
 * @param   lba logical block address
 * @return  block cache buffer */
struct ext4_buf *
ext4_bcache_find_get(struct ext4_bcache *bc, struct ext4_block *b,
		     u64int lba);

/**@brief   Allocate block from block cache memory.
 *          Unreferenced block allocation is based on LRU
 *          (Last Recently Used) algorithm.
 * @param   bc block cache descriptor
 * @param   b block to alloc
 * @param   is_new block is new (needs to be read)
 * @return  standard error code*/
int ext4_bcache_alloc(struct ext4_bcache *bc, struct ext4_block *b,
		      bool *is_new);

/**@brief   Free block from cache memory (decrement reference counter).
 * @param   bc block cache descriptor
 * @param   b block to free
 * @return  standard error code*/
int ext4_bcache_free(struct ext4_bcache *bc, struct ext4_block *b);

/**@brief   Return a full status of block cache.
 * @param   bc block cache descriptor
 * @return  full status*/
bool ext4_bcache_is_full(struct ext4_bcache *bc);
