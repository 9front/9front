#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_misc.h"
#include "ext4_debug.h"
#include "ext4_blockdev.h"
#include "ext4_fs.h"
#include "ext4_journal.h"

static char Eoorop[] = "out of range operation";

static void ext4_bdif_lock(struct ext4_blockdev *bdev)
{
	if (!bdev->bdif->lock)
		return;

	int r = bdev->bdif->lock(bdev);
	assert(r == 0);
}

static void ext4_bdif_unlock(struct ext4_blockdev *bdev)
{
	if (!bdev->bdif->unlock)
		return;

	int r = bdev->bdif->unlock(bdev);
	assert(r == 0);
}

static int ext4_bdif_bread(struct ext4_blockdev *bdev, void *buf,
			   u64int blk_id, u32int blk_cnt)
{
	ext4_bdif_lock(bdev);
	int r = bdev->bdif->bread(bdev, buf, blk_id, blk_cnt);
	bdev->bdif->bread_ctr++;
	ext4_bdif_unlock(bdev);
	return r;
}

static int ext4_bdif_bwrite(struct ext4_blockdev *bdev, const void *buf,
			    u64int blk_id, u32int blk_cnt)
{
	ext4_bdif_lock(bdev);
	int r = bdev->bdif->bwrite(bdev, buf, blk_id, blk_cnt);
	bdev->bdif->bwrite_ctr++;
	ext4_bdif_unlock(bdev);
	return r;
}

int ext4_block_init(struct ext4_blockdev *bdev)
{
	int rc;
	assert(bdev);
	assert(bdev->bdif);
	assert(bdev->bdif->open &&
		   bdev->bdif->close &&
		   bdev->bdif->bread &&
		   bdev->bdif->bwrite);

	if (bdev->bdif->ph_refctr) {
		bdev->bdif->ph_refctr++;
		return 0;
	}

	/*Low level block init*/
	rc = bdev->bdif->open(bdev);
	if (rc != 0)
		return rc;

	bdev->bdif->ph_refctr = 1;
	return 0;
}

int ext4_block_bind_bcache(struct ext4_blockdev *bdev, struct ext4_bcache *bc)
{
	assert(bdev && bc);
	bdev->bc = bc;
	bc->bdev = bdev;
	return 0;
}

void ext4_block_set_lb_size(struct ext4_blockdev *bdev, u32int lb_bsize)
{
	/*Logical block size has to be multiply of physical */
	assert(!(lb_bsize % bdev->bdif->ph_bsize));

	bdev->lg_bsize = lb_bsize;
	bdev->lg_bcnt = bdev->part_size / lb_bsize;
}

int ext4_block_fini(struct ext4_blockdev *bdev)
{
	assert(bdev);

	if (!bdev->bdif->ph_refctr)
		return 0;

	bdev->bdif->ph_refctr--;
	if (bdev->bdif->ph_refctr)
		return 0;

	/*Low level block fini*/
	return bdev->bdif->close(bdev);
}

int ext4_block_flush_buf(struct ext4_blockdev *bdev, struct ext4_buf *buf)
{
	int r;
	struct ext4_bcache *bc = bdev->bc;

	if (ext4_bcache_test_flag(buf, BC_DIRTY) &&
	    ext4_bcache_test_flag(buf, BC_UPTODATE)) {
		r = ext4_blocks_set_direct(bdev, buf->data, buf->lba, 1);
		if (r) {
			if (buf->end_write) {
				bc->dont_shake = true;
				buf->end_write(bc, buf, r, buf->end_write_arg);
				bc->dont_shake = false;
			}

			return r;
		}

		ext4_bcache_remove_dirty_node(bc, buf);
		ext4_bcache_clear_flag(buf, BC_DIRTY);
		if (buf->end_write) {
			bc->dont_shake = true;
			buf->end_write(bc, buf, r, buf->end_write_arg);
			bc->dont_shake = false;
		}
	}
	return 0;
}

int ext4_block_flush_lba(struct ext4_blockdev *bdev, u64int lba)
{
	int r = 0;
	struct ext4_buf *buf;
	struct ext4_block b;
	buf = ext4_bcache_find_get(bdev->bc, &b, lba);
	if (buf) {
		r = ext4_block_flush_buf(bdev, buf);
		ext4_bcache_free(bdev->bc, &b);
	}
	return r;
}

int ext4_block_cache_shake(struct ext4_blockdev *bdev)
{
	int r = 0;
	struct ext4_buf *buf;
	if (bdev->bc->dont_shake)
		return 0;

	bdev->bc->dont_shake = true;

	while (!RB_EMPTY(&bdev->bc->lru_root) &&
		ext4_bcache_is_full(bdev->bc)) {

		buf = ext4_buf_lowest_lru(bdev->bc);
		assert(buf);
		if (ext4_bcache_test_flag(buf, BC_DIRTY)) {
			r = ext4_block_flush_buf(bdev, buf);
			if (r != 0)
				break;

		}

		ext4_bcache_drop_buf(bdev->bc, buf);
	}
	bdev->bc->dont_shake = false;
	return r;
}

int ext4_block_get_noread(struct ext4_blockdev *bdev, struct ext4_block *b,
			  u64int lba)
{
	bool is_new;
	int r;

	assert(bdev && b);

	if (!bdev->bdif->ph_refctr || !(lba < bdev->lg_bcnt)) {
		werrstr(Eio);
		return -1;
	}

	b->lb_id = lba;

	/*If cache is full we have to (flush and) drop it anyway :(*/
	r = ext4_block_cache_shake(bdev);
	if (r != 0)
		return r;

	r = ext4_bcache_alloc(bdev->bc, b, &is_new);
	if (r != 0)
		return r;

	if (!b->data) {
		werrstr("memory");
		return -1;
	}

	return 0;
}

int ext4_block_get(struct ext4_blockdev *bdev, struct ext4_block *b,
		   u64int lba)
{
	int r = ext4_block_get_noread(bdev, b, lba);
	if (r != 0)
		return r;

	if (ext4_bcache_test_flag(b->buf, BC_UPTODATE)) {
		/* Data in the cache is up-to-date.
		 * Reading from physical device is not required */
		return 0;
	}

	r = ext4_blocks_get_direct(bdev, b->data, lba, 1);
	if (r != 0) {
		ext4_bcache_free(bdev->bc, b);
		b->lb_id = 0;
		return r;
	}

	/* Mark buffer up-to-date, since
	 * fresh data is read from physical device just now. */
	ext4_bcache_set_flag(b->buf, BC_UPTODATE);
	return 0;
}

int ext4_block_set(struct ext4_blockdev *bdev, struct ext4_block *b)
{
	assert(bdev && b);
	assert(b->buf);

	if (!bdev->bdif->ph_refctr) {
		werrstr(Eio);
		return -1;
	}

	return ext4_bcache_free(bdev->bc, b);
}

int ext4_blocks_get_direct(struct ext4_blockdev *bdev, void *buf, u64int lba,
			   u32int cnt)
{
	u64int pba;
	u32int pb_cnt;

	assert(bdev && buf);

	pba = (lba * bdev->lg_bsize + bdev->part_offset) / bdev->bdif->ph_bsize;
	pb_cnt = bdev->lg_bsize / bdev->bdif->ph_bsize;

	return ext4_bdif_bread(bdev, buf, pba, pb_cnt * cnt);
}

int ext4_blocks_set_direct(struct ext4_blockdev *bdev, const void *buf,
			   u64int lba, u32int cnt)
{
	u64int pba;
	u32int pb_cnt;

	assert(bdev && buf);

	pba = (lba * bdev->lg_bsize + bdev->part_offset) / bdev->bdif->ph_bsize;
	pb_cnt = bdev->lg_bsize / bdev->bdif->ph_bsize;

	return ext4_bdif_bwrite(bdev, buf, pba, pb_cnt * cnt);
}

int ext4_block_writebytes(struct ext4_blockdev *bdev, u64int off,
			  const void *buf, u32int len)
{
	u64int block_idx;
	u32int blen;
	u32int unalg;
	int r = 0;

	const u8int *p = (void *)buf;

	assert(bdev && buf);

	if (!bdev->bdif->ph_refctr) {
		werrstr(Eio);
		return -1;
	}

	if (off + len > bdev->part_size) {
		werrstr(Eoorop);
		return -1;
	}

	block_idx = ((off + bdev->part_offset) / bdev->bdif->ph_bsize);

	/*OK lets deal with the first possible unaligned block*/
	unalg = (off & (bdev->bdif->ph_bsize - 1));
	if (unalg) {

		u32int wlen = (bdev->bdif->ph_bsize - unalg) > len
				    ? len
				    : (bdev->bdif->ph_bsize - unalg);

		r = ext4_bdif_bread(bdev, bdev->bdif->ph_bbuf, block_idx, 1);
		if (r != 0)
			return r;

		memcpy(bdev->bdif->ph_bbuf + unalg, p, wlen);
		r = ext4_bdif_bwrite(bdev, bdev->bdif->ph_bbuf, block_idx, 1);
		if (r != 0)
			return r;

		p += wlen;
		len -= wlen;
		block_idx++;
	}

	/*Aligned data*/
	blen = len / bdev->bdif->ph_bsize;
	if (blen != 0) {
		r = ext4_bdif_bwrite(bdev, p, block_idx, blen);
		if (r != 0)
			return r;

		p += bdev->bdif->ph_bsize * blen;
		len -= bdev->bdif->ph_bsize * blen;

		block_idx += blen;
	}

	/*Rest of the data*/
	if (len) {
		r = ext4_bdif_bread(bdev, bdev->bdif->ph_bbuf, block_idx, 1);
		if (r != 0)
			return r;

		memcpy(bdev->bdif->ph_bbuf, p, len);
		r = ext4_bdif_bwrite(bdev, bdev->bdif->ph_bbuf, block_idx, 1);
		if (r != 0)
			return r;
	}

	return r;
}

int ext4_block_readbytes(struct ext4_blockdev *bdev, u64int off, void *buf,
			 u32int len)
{
	u64int block_idx;
	u32int blen;
	u32int unalg;
	int r = 0;

	u8int *p = (void *)buf;

	assert(bdev && buf);

	if (!bdev->bdif->ph_refctr) {
		werrstr(Eio);
		return -1;
	}

	if (off + len > bdev->part_size) {
		werrstr(Eoorop);
		return -1;
	}

	block_idx = ((off + bdev->part_offset) / bdev->bdif->ph_bsize);

	/*OK lets deal with the first possible unaligned block*/
	unalg = (off & (bdev->bdif->ph_bsize - 1));
	if (unalg) {

		u32int rlen = (bdev->bdif->ph_bsize - unalg) > len
				    ? len
				    : (bdev->bdif->ph_bsize - unalg);

		r = ext4_bdif_bread(bdev, bdev->bdif->ph_bbuf, block_idx, 1);
		if (r != 0)
			return r;

		memcpy(p, bdev->bdif->ph_bbuf + unalg, rlen);

		p += rlen;
		len -= rlen;
		block_idx++;
	}

	/*Aligned data*/
	blen = len / bdev->bdif->ph_bsize;

	if (blen != 0) {
		r = ext4_bdif_bread(bdev, p, block_idx, blen);
		if (r != 0)
			return r;

		p += bdev->bdif->ph_bsize * blen;
		len -= bdev->bdif->ph_bsize * blen;

		block_idx += blen;
	}

	/*Rest of the data*/
	if (len) {
		r = ext4_bdif_bread(bdev, bdev->bdif->ph_bbuf, block_idx, 1);
		if (r != 0)
			return r;

		memcpy(p, bdev->bdif->ph_bbuf, len);
	}

	return r;
}

int ext4_block_cache_flush(struct ext4_blockdev *bdev)
{
	while (!SLIST_EMPTY(&bdev->bc->dirty_list)) {
		int r;
		struct ext4_buf *buf = SLIST_FIRST(&bdev->bc->dirty_list);
		assert(buf);
		r = ext4_block_flush_buf(bdev, buf);
		if (r != 0)
			return r;

	}
	return 0;
}

int ext4_block_cache_write_back(struct ext4_blockdev *bdev, u8int on_off)
{
	if (on_off)
		bdev->cache_write_back++;

	if (!on_off && bdev->cache_write_back)
		bdev->cache_write_back--;

	if (bdev->cache_write_back)
		return 0;

	/*Flush data in all delayed cache blocks*/
	return ext4_block_cache_flush(bdev);
}
