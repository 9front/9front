#pragma once

#include "ext4_config.h"
#include "ext4_types.h"
#include "queue.h"
#include "tree.h"

struct jbd_fs {
	struct ext4_blockdev *bdev;
	struct ext4_inode_ref inode_ref;
	struct jbd_sb sb;
	u32int uuid_crc32c;

	bool dirty;
};

struct jbd_buf {
	u32int jbd_lba;
	struct ext4_block block;
	struct jbd_trans *trans;
	struct jbd_block_rec *block_rec;
	TAILQ_ENTRY(jbd_buf) buf_node;
	TAILQ_ENTRY(jbd_buf) dirty_buf_node;
};

struct jbd_revoke_rec {
	ext4_fsblk_t lba;
	RB_ENTRY(jbd_revoke_rec) revoke_node;
};

struct jbd_block_rec {
	ext4_fsblk_t lba;
	struct jbd_trans *trans;
	RB_ENTRY(jbd_block_rec) block_rec_node;
	LIST_ENTRY(jbd_block_rec) tbrec_node;
	TAILQ_HEAD(jbd_buf_dirty, jbd_buf) dirty_buf_queue;
};

struct jbd_trans {
	u32int trans_id;

	u32int start_iblock;
	int alloc_blocks;
	int data_cnt;
	u32int data_csum;
	int written_cnt;
	int error;

	struct jbd_journal *journal;

	TAILQ_HEAD(jbd_trans_buf, jbd_buf) buf_queue;
	RB_HEAD(jbd_revoke_tree, jbd_revoke_rec) revoke_root;
	LIST_HEAD(jbd_trans_block_rec, jbd_block_rec) tbrec_list;
	TAILQ_ENTRY(jbd_trans) trans_node;
};

struct jbd_journal {
	u32int first;
	u32int start;
	u32int last;
	u32int trans_id;
	u32int alloc_trans_id;

	u32int block_size;

	TAILQ_HEAD(jbd_cp_queue, jbd_trans) cp_queue;
	RB_HEAD(jbd_block, jbd_block_rec) block_rec_root;

	struct jbd_fs *jbd_fs;
};

int jbd_get_fs(struct ext4_fs *fs,
	       struct jbd_fs *jbd_fs);
int jbd_put_fs(struct jbd_fs *jbd_fs);
int jbd_inode_bmap(struct jbd_fs *jbd_fs,
		   ext4_lblk_t iblock,
		   ext4_fsblk_t *fblock);
int jbd_recover(struct jbd_fs *jbd_fs);
int jbd_journal_start(struct jbd_fs *jbd_fs,
		      struct jbd_journal *journal);
int jbd_journal_stop(struct jbd_journal *journal);
struct jbd_trans *
jbd_journal_new_trans(struct jbd_journal *journal);
int jbd_trans_set_block_dirty(struct jbd_trans *trans,
			      struct ext4_block *block);
int jbd_trans_revoke_block(struct jbd_trans *trans,
			   ext4_fsblk_t lba);
int jbd_trans_try_revoke_block(struct jbd_trans *trans,
			       ext4_fsblk_t lba);
void jbd_journal_free_trans(struct jbd_journal *journal,
			    struct jbd_trans *trans,
			    bool abort);
int jbd_journal_commit_trans(struct jbd_journal *journal,
			     struct jbd_trans *trans);
void
jbd_journal_purge_cp_trans(struct jbd_journal *journal,
			   bool flush,
			   bool once);
