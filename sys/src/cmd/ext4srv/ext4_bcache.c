#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_bcache.h"
#include "ext4_blockdev.h"
#include "ext4_debug.h"

static int ext4_bcache_lba_compare(struct ext4_buf *a, struct ext4_buf *b)
{
	 if (a->lba > b->lba)
		 return 1;
	 else if (a->lba < b->lba)
		 return -1;
	 return 0;
}

static int ext4_bcache_lru_compare(struct ext4_buf *a, struct ext4_buf *b)
{
	if (a->lru_id > b->lru_id)
		return 1;
	else if (a->lru_id < b->lru_id)
		return -1;
	return 0;
}

RB_GENERATE_INTERNAL(ext4_buf_lba, ext4_buf, lba_node,
		     ext4_bcache_lba_compare, static inline)
RB_GENERATE_INTERNAL(ext4_buf_lru, ext4_buf, lru_node,
		     ext4_bcache_lru_compare, static inline)

int ext4_bcache_init_dynamic(struct ext4_bcache *bc, u32int cnt,
			     u32int itemsize)
{
	assert(bc && cnt && itemsize);

	memset(bc, 0, sizeof(struct ext4_bcache));

	bc->cnt = cnt;
	bc->itemsize = itemsize;
	bc->ref_blocks = 0;
	bc->max_ref_blocks = 0;

	return 0;
}

void ext4_bcache_cleanup(struct ext4_bcache *bc)
{
	struct ext4_buf *buf, *tmp;
	RB_FOREACH_SAFE(buf, ext4_buf_lba, &bc->lba_root, tmp) {
		ext4_block_flush_buf(bc->bdev, buf);
		ext4_bcache_drop_buf(bc, buf);
	}
}

int ext4_bcache_fini_dynamic(struct ext4_bcache *bc)
{
	memset(bc, 0, sizeof(struct ext4_bcache));
	return 0;
}

/**@brief:
 *
 *  This is ext4_bcache, the module handling basic buffer-cache stuff.
 *
 *  Buffers in a bcache are sorted by their LBA and stored in a
 *  RB-Tree(lba_root).
 *
 *  Bcache also maintains another RB-Tree(lru_root) right now, where
 *  buffers are sorted by their LRU id.
 *
 *  A singly-linked list is used to track those dirty buffers which are
 *  ready to be flushed. (Those buffers which are dirty but also referenced
 *  are not considered ready to be flushed.)
 *
 *  When a buffer is not referenced, it will be stored in both lba_root
 *  and lru_root, while it will only be stored in lba_root when it is
 *  referenced.
 */

static struct ext4_buf *
ext4_buf_alloc(struct ext4_bcache *bc, u64int lba)
{
	void *data;
	struct ext4_buf *buf;
	data = ext4_malloc(bc->itemsize);
	if (!data)
		return nil;

	buf = ext4_calloc(1, sizeof(struct ext4_buf));
	if (!buf) {
		ext4_free(data);
		return nil;
	}

	buf->lba = lba;
	buf->data = data;
	buf->bc = bc;
	return buf;
}

static void ext4_buf_free(struct ext4_buf *buf)
{
	ext4_free(buf->data);
	ext4_free(buf);
}

static struct ext4_buf *
ext4_buf_lookup(struct ext4_bcache *bc, u64int lba)
{
	struct ext4_buf tmp = {
		.lba = lba
	};

	return RB_FIND(ext4_buf_lba, &bc->lba_root, &tmp);
}

struct ext4_buf *ext4_buf_lowest_lru(struct ext4_bcache *bc)
{
	return RB_MIN(ext4_buf_lru, &bc->lru_root);
}

void ext4_bcache_drop_buf(struct ext4_bcache *bc, struct ext4_buf *buf)
{
	/* Warn on dropping any referenced buffers.*/
	if (buf->refctr) {
		ext4_dbg(DEBUG_BCACHE, DBG_WARN "Buffer is still referenced. "
				"lba: %llud, refctr: %ud\n",
				buf->lba, buf->refctr);
	} else
		RB_REMOVE(ext4_buf_lru, &bc->lru_root, buf);

	RB_REMOVE(ext4_buf_lba, &bc->lba_root, buf);

	/*Forcibly drop dirty buffer.*/
	if (ext4_bcache_test_flag(buf, BC_DIRTY))
		ext4_bcache_remove_dirty_node(bc, buf);

	ext4_buf_free(buf);
	bc->ref_blocks--;
}

void ext4_bcache_invalidate_buf(struct ext4_bcache *bc,
				struct ext4_buf *buf)
{
	buf->end_write = nil;
	buf->end_write_arg = nil;

	/* Clear both dirty and up-to-date flags. */
	if (ext4_bcache_test_flag(buf, BC_DIRTY))
		ext4_bcache_remove_dirty_node(bc, buf);

	ext4_bcache_clear_dirty(buf);
}

void ext4_bcache_invalidate_lba(struct ext4_bcache *bc,
				u64int from,
				u32int cnt)
{
	u64int end = from + cnt - 1;
	struct ext4_buf *tmp = ext4_buf_lookup(bc, from), *buf;
	RB_FOREACH_FROM(buf, ext4_buf_lba, tmp) {
		if (buf->lba > end)
			break;

		ext4_bcache_invalidate_buf(bc, buf);
	}
}

struct ext4_buf *
ext4_bcache_find_get(struct ext4_bcache *bc, struct ext4_block *b,
		     u64int lba)
{
	struct ext4_buf *buf = ext4_buf_lookup(bc, lba);
	if (buf) {
		/* If buffer is not referenced. */
		if (!buf->refctr) {
			/* Assign new value to LRU id and increment LRU counter
			 * by 1*/
			buf->lru_id = ++bc->lru_ctr;
			RB_REMOVE(ext4_buf_lru, &bc->lru_root, buf);
			if (ext4_bcache_test_flag(buf, BC_DIRTY))
				ext4_bcache_remove_dirty_node(bc, buf);

		}

		ext4_bcache_inc_ref(buf);

		b->lb_id = lba;
		b->buf = buf;
		b->data = buf->data;
	}
	return buf;
}

int ext4_bcache_alloc(struct ext4_bcache *bc, struct ext4_block *b,
		      bool *is_new)
{
	/* Try to search the buffer with exaxt LBA. */

	struct ext4_buf *buf = ext4_bcache_find_get(bc, b, b->lb_id);
	if (buf) {
		*is_new = false;
		return 0;
	}

	/* We need to allocate one buffer.*/
	buf = ext4_buf_alloc(bc, b->lb_id);
	if (!buf) {
		werrstr("memory");
		return -1;
	}

	RB_INSERT(ext4_buf_lba, &bc->lba_root, buf);
	/* One more buffer in bcache now. :-) */
	bc->ref_blocks++;

	/*Calc ref blocks max depth*/
	if (bc->max_ref_blocks < bc->ref_blocks)
		bc->max_ref_blocks = bc->ref_blocks;


	ext4_bcache_inc_ref(buf);
	/* Assign new value to LRU id and increment LRU counter
	 * by 1*/
	buf->lru_id = ++bc->lru_ctr;

	b->buf = buf;
	b->data = buf->data;

	*is_new = true;
	return 0;
}

int ext4_bcache_free(struct ext4_bcache *bc, struct ext4_block *b)
{
	struct ext4_buf *buf = b->buf;

	assert(bc && b);

	/*Check if valid.*/
	if (!b->lb_id) {
		werrstr("invalid block id");
		return -1;
	}

	/*Block should have a valid pointer to ext4_buf.*/
	assert(buf);

	/*Check if someone don't try free unreferenced block cache.*/
	assert(buf->refctr);

	/*Just decrease reference counter*/
	ext4_bcache_dec_ref(buf);

	/* We are the last one touching this buffer, do the cleanups. */
	if (!buf->refctr) {
		RB_INSERT(ext4_buf_lru, &bc->lru_root, buf);
		/* This buffer is ready to be flushed. */
		if (ext4_bcache_test_flag(buf, BC_DIRTY) &&
		    ext4_bcache_test_flag(buf, BC_UPTODATE)) {
			if (bc->bdev->cache_write_back &&
			    !ext4_bcache_test_flag(buf, BC_FLUSH) &&
			    !ext4_bcache_test_flag(buf, BC_TMP))
				ext4_bcache_insert_dirty_node(bc, buf);
			else {
				ext4_block_flush_buf(bc->bdev, buf);
				ext4_bcache_clear_flag(buf, BC_FLUSH);
			}
		}

		/* The buffer is invalidated...drop it. */
		if (!ext4_bcache_test_flag(buf, BC_UPTODATE) ||
		    ext4_bcache_test_flag(buf, BC_TMP))
			ext4_bcache_drop_buf(bc, buf);

	}

	b->lb_id = 0;
	b->data = 0;

	return 0;
}

bool ext4_bcache_is_full(struct ext4_bcache *bc)
{
	return (bc->cnt <= bc->ref_blocks);
}
