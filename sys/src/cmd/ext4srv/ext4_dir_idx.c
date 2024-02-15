#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_misc.h"
#include "ext4_debug.h"
#include "ext4_trans.h"
#include "ext4_dir_idx.h"
#include "ext4_dir.h"
#include "ext4_blockdev.h"
#include "ext4_fs.h"
#include "ext4_super.h"
#include "ext4_inode.h"
#include "ext4_crc32.h"
#include "ext4_hash.h"

/**@brief Get hash version used in directory index.
 * @param root_info Pointer to root info structure of index
 * @return Hash algorithm version
 */
static inline u8int
ext4_dir_dx_rinfo_get_hash_version(struct ext4_dir_idx_rinfo *ri)
{
	return ri->hash_version;
}

/**@brief Set hash version, that will be used in directory index.
 * @param root_info Pointer to root info structure of index
 * @param v Hash algorithm version
 */
static inline void
ext4_dir_dx_rinfo_set_hash_version(struct ext4_dir_idx_rinfo *ri, u8int v)
{
	ri->hash_version = v;
}

/**@brief Get length of root_info structure in bytes.
 * @param root_info Pointer to root info structure of index
 * @return Length of the structure
 */
static inline u8int
ext4_dir_dx_rinfo_get_info_length(struct ext4_dir_idx_rinfo *ri)
{
	return ri->info_length;
}

/**@brief Set length of root_info structure in bytes.
 * @param root_info   Pointer to root info structure of index
 * @param info_length Length of the structure
 */
static inline void
ext4_dir_dx_root_info_set_info_length(struct ext4_dir_idx_rinfo *ri,
				      u8int len)
{
	ri->info_length = len;
}

/**@brief Get number of indirect levels of HTree.
 * @param root_info Pointer to root info structure of index
 * @return Height of HTree (actually only 0 or 1)
 */
static inline u8int
ext4_dir_dx_rinfo_get_indirect_levels(struct ext4_dir_idx_rinfo *ri)
{
	return ri->indirect_levels;
}

/**@brief Set number of indirect levels of HTree.
 * @param root_info Pointer to root info structure of index
 * @param lvl Height of HTree (actually only 0 or 1)
 */
static inline void
ext4_dir_dx_rinfo_set_indirect_levels(struct ext4_dir_idx_rinfo *ri, u8int l)
{
	ri->indirect_levels = l;
}

/**@brief Get maximum number of index node entries.
 * @param climit Pointer to counlimit structure
 * @return Maximum of entries in node
 */
static inline u16int
ext4_dir_dx_climit_get_limit(struct ext4_dir_idx_climit *climit)
{
	return to_le16(climit->limit);
}

/**@brief Set maximum number of index node entries.
 * @param climit Pointer to counlimit structure
 * @param limit Maximum of entries in node
 */
static inline void
ext4_dir_dx_climit_set_limit(struct ext4_dir_idx_climit *climit, u16int limit)
{
	climit->limit = to_le16(limit);
}

/**@brief Get current number of index node entries.
 * @param climit Pointer to counlimit structure
 * @return Number of entries in node
 */
static inline u16int
ext4_dir_dx_climit_get_count(struct ext4_dir_idx_climit *climit)
{
	return to_le16(climit->count);
}

/**@brief Set current number of index node entries.
 * @param climit Pointer to counlimit structure
 * @param count Number of entries in node
 */
static inline void
ext4_dir_dx_climit_set_count(struct ext4_dir_idx_climit *climit, u16int count)
{
	climit->count = to_le16(count);
}

/**@brief Get hash value of index entry.
 * @param entry Pointer to index entry
 * @return Hash value
 */
static inline u32int
ext4_dir_dx_entry_get_hash(struct ext4_dir_idx_entry *entry)
{
	return to_le32(entry->hash);
}

/**@brief Set hash value of index entry.
 * @param entry Pointer to index entry
 * @param hash  Hash value
 */
static inline void
ext4_dir_dx_entry_set_hash(struct ext4_dir_idx_entry *entry, u32int hash)
{
	entry->hash = to_le32(hash);
}

/**@brief Get block address where child node is located.
 * @param entry Pointer to index entry
 * @return Block address of child node
 */
static inline u32int
ext4_dir_dx_entry_get_block(struct ext4_dir_idx_entry *entry)
{
	return to_le32(entry->block);
}

/**@brief Set block address where child node is located.
 * @param entry Pointer to index entry
 * @param block Block address of child node
 */
static inline void
ext4_dir_dx_entry_set_block(struct ext4_dir_idx_entry *entry, u32int block)
{
	entry->block = to_le32(block);
}

/**@brief Sort entry item.*/
struct ext4_dx_sort_entry {
	u32int hash;
	u32int rec_len;
	void *dentry;
};

static int ext4_dir_dx_hash_string(struct ext4_hash_info *hinfo, int len,
				   const char *name)
{
	return ext2_htree_hash(name, len, hinfo->seed, hinfo->hash_version,
			       &hinfo->hash, &hinfo->minor_hash);
}

static u32int ext4_dir_dx_checksum(struct ext4_inode_ref *inode_ref, void *de,
				     int count_offset, int count,
				     struct ext4_dir_idx_tail *t)
{
	u32int orig_cum, csum = 0;
	struct ext4_sblock *sb = &inode_ref->fs->sb;
	int sz;

	/* Compute the checksum only if the filesystem supports it */
	if (ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM)) {
		u32int ino_index = to_le32(inode_ref->index);
		u32int ino_gen;
		ino_gen = to_le32(ext4_inode_get_generation(inode_ref->inode));

		sz = count_offset + (count * sizeof(struct ext4_dir_idx_tail));
		orig_cum = t->checksum;
		t->checksum = 0;
		/* First calculate crc32 checksum against fs uuid */
		csum = inode_ref->fs->uuid_crc32c;
		/* Then calculate crc32 checksum against inode number
		 * and inode generation */
		csum = ext4_crc32_u(csum, ino_index);
		csum = ext4_crc32_u(csum, ino_gen);
		/* After that calculate crc32 checksum against all the dx_entry */
		csum = ext4_crc32c(csum, de, sz);
		/* Finally calculate crc32 checksum for dx_tail */
		csum = ext4_crc32c(csum, t, sizeof(struct ext4_dir_idx_tail));
		t->checksum = orig_cum;
	}
	return csum;
}

static struct ext4_dir_idx_climit *
ext4_dir_dx_get_climit(struct ext4_inode_ref *inode_ref,
			   struct ext4_dir_en *dirent, int *offset)
{
	struct ext4_dir_en *dp;
	struct ext4_dir_idx_root *root;
	struct ext4_sblock *sb = &inode_ref->fs->sb;
	u32int block_size = ext4_sb_get_block_size(sb);
	u16int entry_len = ext4_dir_en_get_entry_len(dirent);
	int count_offset;


	if (entry_len == 12) {
		root = (struct ext4_dir_idx_root *)dirent;
		dp = (struct ext4_dir_en *)&root->dots[1];
		if (ext4_dir_en_get_entry_len(dp) != (block_size - 12))
			return nil;
		if (root->info.reserved_zero)
			return nil;
		if (root->info.info_length != sizeof(struct ext4_dir_idx_rinfo))
			return nil;
		count_offset = 32;
	} else if (entry_len == block_size) {
		count_offset = 8;
	} else {
		return nil;
	}

	if (offset)
		*offset = count_offset;
	return (struct ext4_dir_idx_climit *)(((char *)dirent) + count_offset);
}

/*
 * BIG FAT NOTES:
 *       Currently we do not verify the checksum of HTree node.
 */
static bool ext4_dir_dx_csum_verify(struct ext4_inode_ref *inode_ref,
				    struct ext4_dir_en *de)
{
	struct ext4_sblock *sb = &inode_ref->fs->sb;
	u32int block_size = ext4_sb_get_block_size(sb);
	int coff, limit, cnt;

	if (ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM)) {
		struct ext4_dir_idx_climit *climit;
		climit = ext4_dir_dx_get_climit(inode_ref, de, &coff);
		if (!climit) {
			/* Directory seems corrupted. */
			return true;
		}
		struct ext4_dir_idx_tail *t;
		limit = ext4_dir_dx_climit_get_limit(climit);
		cnt = ext4_dir_dx_climit_get_count(climit);
		if (coff + (limit * sizeof(struct ext4_dir_idx_entry)) >
		    (block_size - sizeof(struct ext4_dir_idx_tail))) {
			/* There is no space to hold the checksum */
			return true;
		}
		t = (void *)(((struct ext4_dir_idx_entry *)climit) + limit);

		u32int c;
		c = to_le32(ext4_dir_dx_checksum(inode_ref, de, coff, cnt, t));
		if (t->checksum != c)
			return false;
	}
	return true;
}


static void ext4_dir_set_dx_csum(struct ext4_inode_ref *inode_ref,
				 struct ext4_dir_en *dirent)
{
	int coff, limit, count;
	struct ext4_sblock *sb = &inode_ref->fs->sb;
	u32int block_size = ext4_sb_get_block_size(sb);

	if (ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM)) {
		struct ext4_dir_idx_climit *climit;
		climit = ext4_dir_dx_get_climit(inode_ref, dirent, &coff);
		if (!climit) {
			/* Directory seems corrupted. */
			return;
		}
		struct ext4_dir_idx_tail *t;
		limit = ext4_dir_dx_climit_get_limit(climit);
		count = ext4_dir_dx_climit_get_count(climit);
		if (coff + (limit * sizeof(struct ext4_dir_idx_entry)) >
		   (block_size - sizeof(struct ext4_dir_idx_tail))) {
			/* There is no space to hold the checksum */
			return;
		}

		t = (void *)(((struct ext4_dir_idx_entry *)climit) + limit);
		t->checksum = to_le32(ext4_dir_dx_checksum(inode_ref, dirent,
					coff, count, t));
	}
}

/****************************************************************************/

int ext4_dir_dx_init(struct ext4_inode_ref *dir, struct ext4_inode_ref *parent)
{
	/* Load block 0, where will be index root located */
	ext4_fsblk_t fblock;
	u32int iblock = 0;
	bool need_append =
		(ext4_inode_get_size(&dir->fs->sb, dir->inode)
			< EXT4_DIR_DX_INIT_BCNT)
		? true : false;
	struct ext4_sblock *sb = &dir->fs->sb;
	u32int block_size = ext4_sb_get_block_size(&dir->fs->sb);
	struct ext4_block block;

	int rc;

	if (!need_append)
		rc = ext4_fs_init_inode_dblk_idx(dir, iblock, &fblock);
	else
		rc = ext4_fs_append_inode_dblk(dir, &fblock, &iblock);

	if (rc != 0)
		return rc;

	rc = ext4_trans_block_get_noread(dir->fs->bdev, &block, fblock);
	if (rc != 0)
		return rc;

	/* Initialize pointers to data structures */
	struct ext4_dir_idx_root *root = (void *)block.data;
	struct ext4_dir_idx_rinfo *info = &(root->info);

	memset(root, 0, sizeof(struct ext4_dir_idx_root));
	struct ext4_dir_en *de;

	/* Initialize dot entries */
	de = (struct ext4_dir_en *)root->dots;
	ext4_dir_write_entry(sb, de, 12, dir, ".", strlen("."));

	de = (struct ext4_dir_en *)(root->dots + 1);
	u16int elen = block_size - 12;
	ext4_dir_write_entry(sb, de, elen, parent, "..", strlen(".."));

	/* Initialize root info structure */
	u8int hash_version = ext4_get8(&dir->fs->sb, default_hash_version);

	ext4_dir_dx_rinfo_set_hash_version(info, hash_version);
	ext4_dir_dx_rinfo_set_indirect_levels(info, 0);
	ext4_dir_dx_root_info_set_info_length(info, 8);

	/* Set limit and current number of entries */
	struct ext4_dir_idx_climit *climit;
	climit = (struct ext4_dir_idx_climit *)root->en;

	ext4_dir_dx_climit_set_count(climit, 1);

	u32int entry_space;
	entry_space = block_size - 2 * sizeof(struct ext4_dir_idx_dot_en) -
			sizeof(struct ext4_dir_idx_rinfo);

	if (ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM))
		entry_space -= sizeof(struct ext4_dir_idx_tail);

	u16int root_limit = entry_space / sizeof(struct ext4_dir_idx_entry);
	ext4_dir_dx_climit_set_limit(climit, root_limit);

	/* Append new block, where will be new entries inserted in the future */
	iblock++;
	if (!need_append)
		rc = ext4_fs_init_inode_dblk_idx(dir, iblock, &fblock);
	else
		rc = ext4_fs_append_inode_dblk(dir, &fblock, &iblock);

	if (rc != 0) {
		ext4_block_set(dir->fs->bdev, &block);
		return rc;
	}

	struct ext4_block new_block;
	rc = ext4_trans_block_get_noread(dir->fs->bdev, &new_block, fblock);
	if (rc != 0) {
		ext4_block_set(dir->fs->bdev, &block);
		return rc;
	}

	/* Fill the whole block with empty entry */
	struct ext4_dir_en *be = (void *)new_block.data;

	if (ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM)) {
		u16int len = block_size - sizeof(struct ext4_dir_entry_tail);
		ext4_dir_en_set_entry_len(be, len);
		ext4_dir_en_set_name_len(sb, be, 0);
		ext4_dir_en_set_inode_type(sb, be, EXT4_DE_UNKNOWN);
		ext4_dir_init_entry_tail(EXT4_DIRENT_TAIL(be, block_size));
		ext4_dir_set_csum(dir, be);
	} else {
		ext4_dir_en_set_entry_len(be, block_size);
	}

	ext4_dir_en_set_inode(be, 0);

	ext4_trans_set_block_dirty(new_block.buf);
	rc = ext4_block_set(dir->fs->bdev, &new_block);
	if (rc != 0) {
		ext4_block_set(dir->fs->bdev, &block);
		return rc;
	}

	/* Connect new block to the only entry in index */
	struct ext4_dir_idx_entry *entry = root->en;
	ext4_dir_dx_entry_set_block(entry, iblock);

	ext4_dir_set_dx_csum(dir, (struct ext4_dir_en *)block.data);
	ext4_trans_set_block_dirty(block.buf);

	return ext4_block_set(dir->fs->bdev, &block);
}

/**@brief Initialize hash info structure necessary for index operations.
 * @param hinfo      Pointer to hinfo to be initialized
 * @param root_block Root block (number 0) of index
 * @param sb         Pointer to superblock
 * @param name_len   Length of name to be computed hash value from
 * @param name       Name to be computed hash value from
 * @return Standard error code
 */
static int ext4_dir_hinfo_init(struct ext4_hash_info *hinfo,
			       struct ext4_block *root_block,
			       struct ext4_sblock *sb, usize name_len,
			       const char *name)
{
	struct ext4_dir_idx_root *root;

	root = (struct ext4_dir_idx_root *)root_block->data;
	if ((root->info.hash_version != EXT2_HTREE_LEGACY) &&
	    (root->info.hash_version != EXT2_HTREE_HALF_MD4) &&
	    (root->info.hash_version != EXT2_HTREE_TEA))
		return EXT4_ERR_BAD_DX_DIR;

	/* Check unused flags */
	if (root->info.unused_flags != 0)
		return EXT4_ERR_BAD_DX_DIR;

	/* Check indirect levels */
	if (root->info.indirect_levels > 1)
		return EXT4_ERR_BAD_DX_DIR;

	/* Check if node limit is correct */
	u32int block_size = ext4_sb_get_block_size(sb);
	u32int entry_space = block_size;
	entry_space -= 2 * sizeof(struct ext4_dir_idx_dot_en);
	entry_space -= sizeof(struct ext4_dir_idx_rinfo);
	if (ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM))
		entry_space -= sizeof(struct ext4_dir_idx_tail);
	entry_space = entry_space / sizeof(struct ext4_dir_idx_entry);

	struct ext4_dir_idx_climit *climit = (void *)root->en;
	u16int limit = ext4_dir_dx_climit_get_limit(climit);
	if (limit != entry_space)
		return EXT4_ERR_BAD_DX_DIR;

	/* Check hash version and modify if necessary */
	hinfo->hash_version = ext4_dir_dx_rinfo_get_hash_version(&root->info);
	if ((hinfo->hash_version <= EXT2_HTREE_TEA) &&
	    (ext4_sb_check_flag(sb, EXT4_SUPERBLOCK_FLAGS_UNSIGNED_HASH))) {
		/* Use unsigned hash */
		hinfo->hash_version += 3;
	}

	/* Load hash seed from superblock */
	hinfo->seed = ext4_get8(sb, hash_seed);

	/* Compute hash value of name */
	if (name)
		return ext4_dir_dx_hash_string(hinfo, name_len, name);

	return 0;
}

/**@brief Walk through index tree and load leaf with corresponding hash value.
 * @param hinfo      Initialized hash info structure
 * @param inode_ref  Current i-node
 * @param root_block Root block (iblock 0), where is root node located
 * @param dx_block   Pointer to leaf node in dx_blocks array
 * @param dx_blocks  Array with the whole path from root to leaf
 * @return Standard error code
 */
static int ext4_dir_dx_get_leaf(struct ext4_hash_info *hinfo,
				struct ext4_inode_ref *inode_ref,
				struct ext4_block *root_block,
				struct ext4_dir_idx_block **dx_block,
				struct ext4_dir_idx_block *dx_blocks)
{
	struct ext4_dir_idx_root *root;
	struct ext4_dir_idx_entry *entries;
	struct ext4_dir_idx_entry *p;
	struct ext4_dir_idx_entry *q;
	struct ext4_dir_idx_entry *m;
	struct ext4_dir_idx_entry *at;
	ext4_fsblk_t fblk;
	u32int block_size;
	u16int limit;
	u16int entry_space;
	u8int ind_level;
	int r;

	struct ext4_dir_idx_block *tmp_dx_blk = dx_blocks;
	struct ext4_block *tmp_blk = root_block;
	struct ext4_sblock *sb = &inode_ref->fs->sb;

	block_size = ext4_sb_get_block_size(sb);
	root = (struct ext4_dir_idx_root *)root_block->data;
	entries = (struct ext4_dir_idx_entry *)root->en;
	limit = ext4_dir_dx_climit_get_limit((void *)entries);
	ind_level = ext4_dir_dx_rinfo_get_indirect_levels(&root->info);

	/* Walk through the index tree */
	while (true) {
		u16int cnt = ext4_dir_dx_climit_get_count((void *)entries);
		if ((cnt == 0) || (cnt > limit))
			return EXT4_ERR_BAD_DX_DIR;

		/* Do binary search in every node */
		p = entries + 1;
		q = entries + cnt - 1;

		while (p <= q) {
			m = p + (q - p) / 2;
			if (ext4_dir_dx_entry_get_hash(m) > hinfo->hash)
				q = m - 1;
			else
				p = m + 1;
		}

		at = p - 1;

		/* Write results */
		memcpy(&tmp_dx_blk->b, tmp_blk, sizeof(struct ext4_block));
		tmp_dx_blk->entries = entries;
		tmp_dx_blk->position = at;

		/* Is algorithm in the leaf? */
		if (ind_level == 0) {
			*dx_block = tmp_dx_blk;
			return 0;
		}

		/* Goto child node */
		u32int n_blk = ext4_dir_dx_entry_get_block(at);

		ind_level--;

		r = ext4_fs_get_inode_dblk_idx(inode_ref, n_blk, &fblk, false);
		if (r != 0)
			return r;

		r = ext4_trans_block_get(inode_ref->fs->bdev, tmp_blk, fblk);
		if (r != 0)
			return r;

		entries = ((struct ext4_dir_idx_node *)tmp_blk->data)->entries;
		limit = ext4_dir_dx_climit_get_limit((void *)entries);

		entry_space = block_size - sizeof(struct ext4_fake_dir_entry);
		if (ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM))
			entry_space -= sizeof(struct ext4_dir_idx_tail);

		entry_space = entry_space / sizeof(struct ext4_dir_idx_entry);

		if (limit != entry_space) {
			ext4_block_set(inode_ref->fs->bdev, tmp_blk);
			return EXT4_ERR_BAD_DX_DIR;
		}

		if (!ext4_dir_dx_csum_verify(inode_ref, (void *)tmp_blk->data)) {
			ext4_dbg(DEBUG_DIR_IDX,
					DBG_WARN "HTree checksum failed."
					"Inode: %ud, "
					"Block: %ud\n",
					inode_ref->index,
					n_blk);
		}

		++tmp_dx_blk;
	}

	/* Unreachable */
}

/**@brief Check if the the next block would be checked during entry search.
 * @param inode_ref Directory i-node
 * @param hash      Hash value to check
 * @param dx_block  Current block
 * @param dx_blocks Array with path from root to leaf node
 * @return Standard Error code
 */
static int ext4_dir_dx_next_block(struct ext4_inode_ref *inode_ref,
				  u32int hash,
				  struct ext4_dir_idx_block *dx_block,
				  struct ext4_dir_idx_block *dx_blocks)
{
	int r;
	u32int num_handles = 0;
	ext4_fsblk_t blk_adr;
	struct ext4_dir_idx_block *p = dx_block;

	/* Try to find data block with next bunch of entries */
	while (true) {
		u16int cnt = ext4_dir_dx_climit_get_count((void *)p->entries);

		p->position++;
		if (p->position < p->entries + cnt)
			break;

		if (p == dx_blocks)
			return 0;

		num_handles++;
		p--;
	}

	/* Check hash collision (if not occurred - no next block cannot be
	 * used)*/
	u32int current_hash = ext4_dir_dx_entry_get_hash(p->position);
	if ((hash & 1) == 0) {
		if ((current_hash & ~1) != hash)
			return 0;
	}

	/* Fill new path */
	while (num_handles--) {
		u32int blk = ext4_dir_dx_entry_get_block(p->position);
		r = ext4_fs_get_inode_dblk_idx(inode_ref, blk, &blk_adr, false);
		if (r != 0)
			return r;

		struct ext4_block b;
		r = ext4_trans_block_get(inode_ref->fs->bdev, &b, blk_adr);
		if (r != 0)
			return r;

		if (!ext4_dir_dx_csum_verify(inode_ref, (void *)b.data)) {
			ext4_dbg(DEBUG_DIR_IDX,
					DBG_WARN "HTree checksum failed."
					"Inode: %ud, "
					"Block: %ud\n",
					inode_ref->index,
					blk);
		}

		p++;

		/* Don't forget to put old block (prevent memory leak) */
		r = ext4_block_set(inode_ref->fs->bdev, &p->b);
		if (r != 0)
			return r;

		memcpy(&p->b, &b, sizeof(b));
		p->entries = ((struct ext4_dir_idx_node *)b.data)->entries;
		p->position = p->entries;
	}

	werrstr(Enotfound);
	return EXT4_ERR_NOT_FOUND;
}

int ext4_dir_dx_find_entry(struct ext4_dir_search_result *result,
			   struct ext4_inode_ref *inode_ref, usize name_len,
			   const char *name)
{
	/* Load direct block 0 (index root) */
	ext4_fsblk_t root_block_addr;
	int rc2;
	int rc;
	rc = ext4_fs_get_inode_dblk_idx(inode_ref,  0, &root_block_addr, false);
	if (rc != 0)
		return rc;

	struct ext4_fs *fs = inode_ref->fs;

	struct ext4_block root_block;
	rc = ext4_trans_block_get(fs->bdev, &root_block, root_block_addr);
	if (rc != 0)
		return rc;

	if (!ext4_dir_dx_csum_verify(inode_ref, (void *)root_block.data)) {
		ext4_dbg(DEBUG_DIR_IDX,
			 DBG_WARN "HTree root checksum failed."
			 "Inode: %ud, "
			 "Block: %ud\n",
			 inode_ref->index,
			 (u32int)0);
	}

	/* Initialize hash info (compute hash value) */
	struct ext4_hash_info hinfo;
	rc = ext4_dir_hinfo_init(&hinfo, &root_block, &fs->sb, name_len, name);
	if (rc != 0) {
		ext4_block_set(fs->bdev, &root_block);
		return EXT4_ERR_BAD_DX_DIR;
	}

	/*
	 * Hardcoded number 2 means maximum height of index tree,
	 * specified in the Linux driver.
	 */
	struct ext4_dir_idx_block dx_blocks[2];
	struct ext4_dir_idx_block *dx_block;
	struct ext4_dir_idx_block *tmp;

	rc = ext4_dir_dx_get_leaf(&hinfo, inode_ref, &root_block, &dx_block,
				  dx_blocks);
	if (rc != 0) {
		ext4_block_set(fs->bdev, &root_block);
		return EXT4_ERR_BAD_DX_DIR;
	}

	for (;;) {
		/* Load leaf block */
		u32int leaf_blk_idx;
		ext4_fsblk_t leaf_block_addr;
		struct ext4_block b;

		leaf_blk_idx = ext4_dir_dx_entry_get_block(dx_block->position);
		rc = ext4_fs_get_inode_dblk_idx(inode_ref, leaf_blk_idx,
						&leaf_block_addr, false);
		if (rc != 0)
			break;

		rc = ext4_trans_block_get(fs->bdev, &b, leaf_block_addr);
		if (rc != 0)
			break;

		if (!ext4_dir_csum_verify(inode_ref, (void *)b.data)) {
			ext4_dbg(DEBUG_DIR_IDX,
				 DBG_WARN "HTree leaf block checksum failed."
				 "Inode: %ud, "
				 "Block: %ud\n",
				 inode_ref->index,
				 leaf_blk_idx);
		}

		/* Linear search inside block */
		struct ext4_dir_en *de;
		rc = ext4_dir_find_in_block(&b, &fs->sb, name_len, name, &de);

		/* Found => return it */
		if (rc == 0) {
			result->block = b;
			result->dentry = de;
			break;
		}

		/* Not found, leave untouched */
		rc2 = ext4_block_set(fs->bdev, &b);
		if (rc2 != 0)
			break;

		if (rc != EXT4_ERR_NOT_FOUND)
			break;

		/* check if the next block could be checked */
		rc = ext4_dir_dx_next_block(inode_ref, hinfo.hash, dx_block, &dx_blocks[0]);
		if (rc != 0) {
			if (rc == EXT4_ERR_NOT_FOUND)
				continue;
			break;
		}
	}

	/* The whole path must be released (preventing memory leak) */
	tmp = dx_blocks;

	while (tmp <= dx_block) {
		rc2 = ext4_block_set(fs->bdev, &tmp->b);
		if (rc == 0 && rc2 != 0)
			rc = rc2;
		++tmp;
	}

	return rc;
}

/**@brief  Compare function used to pass in quicksort implementation.
 *         It can compare two entries by hash value.
 * @param arg1  First entry
 * @param arg2  Second entry
 * @param dummy Unused parameter, can be nil
 *
 * @return Classic compare result
 *         (0: equal, -1: arg1 < arg2, 1: arg1 > arg2)
 */
static int ext4_dir_dx_entry_comparator(const void *arg1, const void *arg2)
{
	struct ext4_dx_sort_entry *entry1 = (void *)arg1;
	struct ext4_dx_sort_entry *entry2 = (void *)arg2;

	if (entry1->hash == entry2->hash)
		return 0;

	if (entry1->hash < entry2->hash)
		return -1;
	else
		return 1;
}

/**@brief  Insert new index entry to block.
 *         Note that space for new entry must be checked by caller.
 * @param inode_ref   Directory i-node
 * @param index_block Block where to insert new entry
 * @param hash        Hash value covered by child node
 * @param iblock      Logical number of child block
 *
 */
static void
ext4_dir_dx_insert_entry(struct ext4_inode_ref *inode_ref,
			 struct ext4_dir_idx_block *index_block,
			 u32int hash, u32int iblock)
{
	struct ext4_dir_idx_entry *old_index_entry = index_block->position;
	struct ext4_dir_idx_entry *new_index_entry = old_index_entry + 1;
	struct ext4_dir_idx_climit *climit = (void *)index_block->entries;
	struct ext4_dir_idx_entry *start_index = index_block->entries;
	u32int count = ext4_dir_dx_climit_get_count(climit);

	usize bytes;
	bytes = (u8int *)(start_index + count) - (u8int *)(new_index_entry);

	memmove(new_index_entry + 1, new_index_entry, bytes);

	ext4_dir_dx_entry_set_block(new_index_entry, iblock);
	ext4_dir_dx_entry_set_hash(new_index_entry, hash);
	ext4_dir_dx_climit_set_count(climit, count + 1);
	ext4_dir_set_dx_csum(inode_ref, (void *)index_block->b.data);
	ext4_trans_set_block_dirty(index_block->b.buf);
}

/**@brief Split directory entries to two parts preventing node overflow.
 * @param inode_ref      Directory i-node
 * @param hinfo          Hash info
 * @param old_data_block Block with data to be split
 * @param index_block    Block where index entries are located
 * @param new_data_block Output value for newly allocated data block
 */
static int ext4_dir_dx_split_data(struct ext4_inode_ref *inode_ref,
				  struct ext4_hash_info *hinfo,
				  struct ext4_block *old_data_block,
				  struct ext4_dir_idx_block *index_block,
				  struct ext4_block *new_data_block)
{
	int rc;
	struct ext4_sblock *sb = &inode_ref->fs->sb;
	u32int block_size = ext4_sb_get_block_size(&inode_ref->fs->sb);

	/* Allocate buffer for directory entries */
	u8int *entry_buffer = ext4_malloc(block_size);
	if (entry_buffer == nil) {
		werrstr(Enomem);
		return -1;
	}

	/* dot entry has the smallest size available */
	u32int max_ecnt = block_size / sizeof(struct ext4_dir_idx_dot_en);

	/* Allocate sort entry */
	struct ext4_dx_sort_entry *sort;

	sort = ext4_malloc(max_ecnt * sizeof(struct ext4_dx_sort_entry));
	if (sort == nil) {
		ext4_free(entry_buffer);
		werrstr(Enomem);
		return -1;
	}

	u32int idx = 0;
	u32int real_size = 0;

	/* Initialize hinfo */
	struct ext4_hash_info hinfo_tmp;
	memcpy(&hinfo_tmp, hinfo, sizeof(struct ext4_hash_info));

	/* Load all valid entries to the buffer */
	struct ext4_dir_en *de = (void *)old_data_block->data;
	u8int *entry_buffer_ptr = entry_buffer;
	while ((void *)de < (void *)(old_data_block->data + block_size)) {
		/* Read only valid entries */
		if (ext4_dir_en_get_inode(de) && de->name_len) {
			u16int len = ext4_dir_en_get_name_len(sb, de);
			rc = ext4_dir_dx_hash_string(&hinfo_tmp, len,
						     (char *)de->name);
			if (rc != 0) {
				ext4_free(sort);
				ext4_free(entry_buffer);
				return rc;
			}

			u32int rec_len = 8 + len;
			if ((rec_len % 4) != 0)
				rec_len += 4 - (rec_len % 4);

			memcpy(entry_buffer_ptr, de, rec_len);

			sort[idx].dentry = entry_buffer_ptr;
			sort[idx].rec_len = rec_len;
			sort[idx].hash = hinfo_tmp.hash;

			entry_buffer_ptr += rec_len;
			real_size += rec_len;
			idx++;
		}

		usize elen = ext4_dir_en_get_entry_len(de);
		de = (void *)((u8int *)de + elen);
	}

	qsort(sort, idx, sizeof(struct ext4_dx_sort_entry),
	      ext4_dir_dx_entry_comparator);

	/* Allocate new block for store the second part of entries */
	ext4_fsblk_t new_fblock;
	u32int new_iblock;
	rc = ext4_fs_append_inode_dblk(inode_ref, &new_fblock, &new_iblock);
	if (rc != 0) {
		ext4_free(sort);
		ext4_free(entry_buffer);
		return rc;
	}

	/* Load new block */
	struct ext4_block new_data_block_tmp;
	rc = ext4_trans_block_get_noread(inode_ref->fs->bdev, &new_data_block_tmp,
				   new_fblock);
	if (rc != 0) {
		ext4_free(sort);
		ext4_free(entry_buffer);
		return rc;
	}

	/*
	 * Distribute entries to two blocks (by size)
	 * - compute the half
	 */
	u32int new_hash = 0;
	u32int current_size = 0;
	u32int mid = 0;
	u32int i;
	for (i = 0; i < idx; ++i) {
		if ((current_size + sort[i].rec_len) > (block_size / 2)) {
			new_hash = sort[i].hash;
			mid = i;
			break;
		}

		current_size += sort[i].rec_len;
	}

	/* Check hash collision */
	u32int continued = 0;
	if (new_hash == sort[mid - 1].hash)
		continued = 1;

	u32int off = 0;
	void *ptr;
	if (ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM))
		block_size -= sizeof(struct ext4_dir_entry_tail);

	/* First part - to the old block */
	for (i = 0; i < mid; ++i) {
		ptr = old_data_block->data + off;
		memcpy(ptr, sort[i].dentry, sort[i].rec_len);

		struct ext4_dir_en *t = ptr;
		if (i < (mid - 1))
			ext4_dir_en_set_entry_len(t, sort[i].rec_len);
		else
			ext4_dir_en_set_entry_len(t, block_size - off);

		off += sort[i].rec_len;
	}

	/* Second part - to the new block */
	off = 0;
	for (i = mid; i < idx; ++i) {
		ptr = new_data_block_tmp.data + off;
		memcpy(ptr, sort[i].dentry, sort[i].rec_len);

		struct ext4_dir_en *t = ptr;
		if (i < (idx - 1))
			ext4_dir_en_set_entry_len(t, sort[i].rec_len);
		else
			ext4_dir_en_set_entry_len(t, block_size - off);

		off += sort[i].rec_len;
	}

	block_size = ext4_sb_get_block_size(&inode_ref->fs->sb);

	/* Do some steps to finish operation */
	sb = &inode_ref->fs->sb;
	if (ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM)) {
		struct ext4_dir_entry_tail *t;

		t = EXT4_DIRENT_TAIL(old_data_block->data, block_size);
		ext4_dir_init_entry_tail(t);
		t = EXT4_DIRENT_TAIL(new_data_block_tmp.data, block_size);
		ext4_dir_init_entry_tail(t);
	}
	ext4_dir_set_csum(inode_ref, (void *)old_data_block->data);
	ext4_dir_set_csum(inode_ref, (void *)new_data_block_tmp.data);
	ext4_trans_set_block_dirty(old_data_block->buf);
	ext4_trans_set_block_dirty(new_data_block_tmp.buf);

	ext4_free(sort);
	ext4_free(entry_buffer);

	ext4_dir_dx_insert_entry(inode_ref, index_block, new_hash + continued,
				new_iblock);

	*new_data_block = new_data_block_tmp;
	return 0;
}

/**@brief  Split index node and maybe some parent nodes in the tree hierarchy.
 * @param inode_ref Directory i-node
 * @param dx_blocks Array with path from root to leaf node
 * @param dx_block  Leaf block to be split if needed
 * @return Error code
 */
static int
ext4_dir_dx_split_index(struct ext4_inode_ref *ino_ref,
			struct ext4_dir_idx_block *dx_blks,
			struct ext4_dir_idx_block *dxb,
			struct ext4_dir_idx_block **new_dx_block)
{
	struct ext4_sblock *sb = &ino_ref->fs->sb;
	struct ext4_dir_idx_entry *e;
	int r;

	u32int block_size = ext4_sb_get_block_size(&ino_ref->fs->sb);
	u32int entry_space = block_size - sizeof(struct ext4_fake_dir_entry);
	u32int node_limit =  entry_space / sizeof(struct ext4_dir_idx_entry);

	bool meta_csum = ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM);

	if (dxb == dx_blks)
		e = ((struct ext4_dir_idx_root *)dxb->b.data)->en;
	else
		e = ((struct ext4_dir_idx_node *)dxb->b.data)->entries;

	struct ext4_dir_idx_climit *climit = (struct ext4_dir_idx_climit *)e;

	u16int leaf_limit = ext4_dir_dx_climit_get_limit(climit);
	u16int leaf_count = ext4_dir_dx_climit_get_count(climit);

	/* Check if is necessary to split index block */
	if (leaf_limit == leaf_count) {
		struct ext4_dir_idx_entry *ren;
		intptr levels = dxb - dx_blks;

		ren = ((struct ext4_dir_idx_root *)dx_blks[0].b.data)->en;
		struct ext4_dir_idx_climit *rclimit = (void *)ren;
		u16int root_limit = ext4_dir_dx_climit_get_limit(rclimit);
		u16int root_count = ext4_dir_dx_climit_get_count(rclimit);

		/* Linux limitation */
		if ((levels > 0) && (root_limit == root_count)) {
			werrstr(Enospc);
			return -1;
		}

		/* Add new block to directory */
		ext4_fsblk_t new_fblk;
		u32int new_iblk;
		r = ext4_fs_append_inode_dblk(ino_ref, &new_fblk, &new_iblk);
		if (r != 0)
			return r;

		/* load new block */
		struct ext4_block b;
		r = ext4_trans_block_get_noread(ino_ref->fs->bdev, &b, new_fblk);
		if (r != 0)
			return r;

		struct ext4_dir_idx_node *new_node = (void *)b.data;
		struct ext4_dir_idx_entry *new_en = new_node->entries;

		memset(&new_node->fake, 0, sizeof(struct ext4_fake_dir_entry));
		new_node->fake.entry_length = block_size;

		/* Split leaf node */
		if (levels > 0) {
			u32int count_left = leaf_count / 2;
			u32int count_right = leaf_count - count_left;
			u32int hash_right;
			usize sz;

			struct ext4_dir_idx_climit *left_climit;
			struct ext4_dir_idx_climit *right_climit;

			hash_right = ext4_dir_dx_entry_get_hash(e + count_left);
			/* Copy data to new node */
			sz = count_right * sizeof(struct ext4_dir_idx_entry);
			memcpy(new_en, e + count_left, sz);

			/* Initialize new node */
			left_climit = (struct ext4_dir_idx_climit *)e;
			right_climit = (struct ext4_dir_idx_climit *)new_en;

			ext4_dir_dx_climit_set_count(left_climit, count_left);
			ext4_dir_dx_climit_set_count(right_climit, count_right);

			if (meta_csum)
				entry_space -= sizeof(struct ext4_dir_idx_tail);
			USED(entry_space);

			ext4_dir_dx_climit_set_limit(right_climit, node_limit);

			/* Which index block is target for new entry */
			u32int position_index =
			    (dxb->position - dxb->entries);
			if (position_index >= count_left) {
				ext4_dir_set_dx_csum(
						ino_ref,
						(struct ext4_dir_en *)
						dxb->b.data);
				ext4_trans_set_block_dirty(dxb->b.buf);

				struct ext4_block block_tmp = dxb->b;

				dxb->b = b;

				dxb->position =
				    new_en + position_index - count_left;
				dxb->entries = new_en;

				b = block_tmp;
			}

			/* Finally insert new entry */
			ext4_dir_dx_insert_entry(ino_ref, dx_blks, hash_right,
						 new_iblk);
			ext4_dir_set_dx_csum(ino_ref, (void*)dx_blks[0].b.data);
			ext4_dir_set_dx_csum(ino_ref, (void*)dx_blks[1].b.data);
			ext4_trans_set_block_dirty(dx_blks[0].b.buf);
			ext4_trans_set_block_dirty(dx_blks[1].b.buf);

			ext4_dir_set_dx_csum(ino_ref, (void *)b.data);
			ext4_trans_set_block_dirty(b.buf);
			return ext4_block_set(ino_ref->fs->bdev, &b);
		} else {
			usize sz;
			/* Copy data from root to child block */
			sz = leaf_count * sizeof(struct ext4_dir_idx_entry);
			memcpy(new_en, e, sz);

			struct ext4_dir_idx_climit *new_climit = (void*)new_en;
			if (meta_csum)
				entry_space -= sizeof(struct ext4_dir_idx_tail);
			USED(entry_space);

			ext4_dir_dx_climit_set_limit(new_climit, node_limit);

			/* Set values in root node */
			struct ext4_dir_idx_climit *new_root_climit = (void *)e;

			ext4_dir_dx_climit_set_count(new_root_climit, 1);
			ext4_dir_dx_entry_set_block(e, new_iblk);

			struct ext4_dir_idx_root *r = (void *)dx_blks[0].b.data;
			r->info.indirect_levels = 1;

			/* Add new entry to the path */
			dxb = dx_blks + 1;
			dxb->position = dx_blks->position - e + new_en;
			dxb->entries = new_en;
			dxb->b = b;
			*new_dx_block = dxb;

			ext4_dir_set_dx_csum(ino_ref, (void*)dx_blks[0].b.data);
			ext4_dir_set_dx_csum(ino_ref, (void*)dx_blks[1].b.data);
			ext4_trans_set_block_dirty(dx_blks[0].b.buf);
			ext4_trans_set_block_dirty(dx_blks[1].b.buf);
		}
	}

	return 0;
}

int ext4_dir_dx_add_entry(struct ext4_inode_ref *parent,
			  struct ext4_inode_ref *child, const char *name, u32int name_len)
{
	int rc2 = 0;
	int r;
	/* Get direct block 0 (index root) */
	ext4_fsblk_t rblock_addr;
	r =  ext4_fs_get_inode_dblk_idx(parent, 0, &rblock_addr, false);
	if (r != 0)
		return r;

	struct ext4_fs *fs = parent->fs;
	struct ext4_block root_blk;

	r = ext4_trans_block_get(fs->bdev, &root_blk, rblock_addr);
	if (r != 0)
		return r;

	if (!ext4_dir_dx_csum_verify(parent, (void*)root_blk.data)) {
		ext4_dbg(DEBUG_DIR_IDX,
			 DBG_WARN "HTree root checksum failed."
			 "Inode: %ud, "
			 "Block: %ud\n",
			 parent->index,
			 (u32int)0);
	}

	/* Initialize hinfo structure (mainly compute hash) */
	struct ext4_hash_info hinfo;
	r = ext4_dir_hinfo_init(&hinfo, &root_blk, &fs->sb, name_len, name);
	if (r != 0) {
		ext4_block_set(fs->bdev, &root_blk);
		return EXT4_ERR_BAD_DX_DIR;
	}

	/*
	 * Hardcoded number 2 means maximum height of index
	 * tree defined in Linux.
	 */
	struct ext4_dir_idx_block dx_blks[2];
	struct ext4_dir_idx_block *dx_blk;
	struct ext4_dir_idx_block *dx_it;

	r = ext4_dir_dx_get_leaf(&hinfo, parent, &root_blk, &dx_blk, dx_blks);
	if (r != 0) {
		r = EXT4_ERR_BAD_DX_DIR;
		goto release_index;
	}

	/* Try to insert to existing data block */
	u32int leaf_block_idx = ext4_dir_dx_entry_get_block(dx_blk->position);
	ext4_fsblk_t leaf_block_addr;
	r = ext4_fs_get_inode_dblk_idx(parent, leaf_block_idx,
						&leaf_block_addr, false);
	if (r != 0)
		goto release_index;

	/*
	 * Check if there is needed to split index node
	 * (and recursively also parent nodes)
	 */
	r = ext4_dir_dx_split_index(parent, dx_blks, dx_blk, &dx_blk);
	if (r != 0)
		goto release_target_index;

	struct ext4_block target_block;
	r = ext4_trans_block_get(fs->bdev, &target_block, leaf_block_addr);
	if (r != 0)
		goto release_index;

	if (!ext4_dir_csum_verify(parent,(void *)target_block.data)) {
		ext4_dbg(DEBUG_DIR_IDX,
				DBG_WARN "HTree leaf block checksum failed."
				"Inode: %ud, "
				"Block: %ud\n",
				parent->index,
				leaf_block_idx);
	}

	/* Check if insert operation passed */
	r = ext4_dir_try_insert_entry(&fs->sb, parent, &target_block, child,
					name, name_len);
	if (r == 0)
		goto release_target_index;

	/* Split entries to two blocks (includes sorting by hash value) */
	struct ext4_block new_block;
	r = ext4_dir_dx_split_data(parent, &hinfo, &target_block, dx_blk,
				    &new_block);
	if (r != 0)
		goto release_target_index;

	/* Where to save new entry */
	u32int blk_hash = ext4_dir_dx_entry_get_hash(dx_blk->position + 1);
	if (hinfo.hash >= blk_hash)
		r = ext4_dir_try_insert_entry(&fs->sb, parent, &new_block,
						child, name, name_len);
	else
		r = ext4_dir_try_insert_entry(&fs->sb, parent, &target_block,
						child, name, name_len);
	if (r != 0)
		goto release_target_index;

	/* Cleanup */
	r = ext4_block_set(fs->bdev, &new_block);
	if (r != 0)
		return r;

/* Cleanup operations */

release_target_index:
	rc2 = r;

	r = ext4_block_set(fs->bdev, &target_block);
	if (r != 0)
		return r;

release_index:
	if (r != 0)
		rc2 = r;

	dx_it = dx_blks;

	while (dx_it <= dx_blk) {
		r = ext4_block_set(fs->bdev, &dx_it->b);
		if (r != 0)
			return r;

		dx_it++;
	}

	return rc2;
}

int ext4_dir_dx_reset_parent_inode(struct ext4_inode_ref *dir,
                                   u32int parent_inode)
{
	/* Load block 0, where will be index root located */
	ext4_fsblk_t fblock;
	int rc = ext4_fs_get_inode_dblk_idx(dir, 0, &fblock, false);
	if (rc != 0)
		return rc;

	struct ext4_block block;
	rc = ext4_trans_block_get(dir->fs->bdev, &block, fblock);
	if (rc != 0)
		return rc;

	if (!ext4_dir_dx_csum_verify(dir, (void *)block.data)) {
		ext4_dbg(DEBUG_DIR_IDX,
			 DBG_WARN "HTree root checksum failed."
			 "Inode: %ud, "
			 "Block: %ud\n",
			 dir->index,
			 (u32int)0);
	}

	/* Initialize pointers to data structures */
	struct ext4_dir_idx_root *root = (void *)block.data;

	/* Fill the inode field with a new parent ino. */
	ext4_dx_dot_en_set_inode(&root->dots[1], parent_inode);

	ext4_dir_set_dx_csum(dir, (void *)block.data);
	ext4_trans_set_block_dirty(block.buf);

	return ext4_block_set(dir->fs->bdev, &block);
}
