#include "ext4_config.h"
#include "ext4.h"
#include "ext4_fs.h"
#include "ext4_journal.h"

int ext4_trans_set_block_dirty(struct ext4_buf *buf)
{
	int r = 0;

	struct ext4_fs *fs = buf->bc->bdev->fs;
	struct ext4_block block = {
		.lb_id = buf->lba,
		.data = buf->data,
		.buf = buf
	};

	if (fs->jbd_journal && fs->curr_trans) {
		struct jbd_trans *trans = fs->curr_trans;
		return jbd_trans_set_block_dirty(trans, &block);
	}

	ext4_bcache_set_dirty(buf);
	return r;
}

int ext4_trans_block_get_noread(struct ext4_blockdev *bdev,
			  struct ext4_block *b,
			  u64int lba)
{
	int r = ext4_block_get_noread(bdev, b, lba);
	if (r != 0)
		return r;

	return r;
}

int ext4_trans_block_get(struct ext4_blockdev *bdev,
		   struct ext4_block *b,
		   u64int lba)
{
	int r = ext4_block_get(bdev, b, lba);
	if (r != 0)
		return r;

	return r;
}

int ext4_trans_try_revoke_block(struct ext4_blockdev *bdev, u64int lba)
{
	int r = 0;

	struct ext4_fs *fs = bdev->fs;
	if (fs->jbd_journal && fs->curr_trans) {
		struct jbd_trans *trans = fs->curr_trans;
		r = jbd_trans_try_revoke_block(trans, lba);
	} else if (fs->jbd_journal) {
		r = ext4_block_flush_lba(fs->bdev, lba);
	}

	return r;
}
