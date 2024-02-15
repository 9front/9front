#include "ext4_config.h"
#include "ext4_debug.h"
#include "ext4_fs.h"
#include "ext4_trans.h"
#include "ext4_blockdev.h"
#include "ext4_extent.h"
#include "ext4_inode.h"
#include "ext4_super.h"
#include "ext4_crc32.h"
#include "ext4_balloc.h"

//#define CONFIG_EXTENT_DEBUG_VERBOSE

/**@brief Return the extent tree depth
 * @param inode_ref I-node reference the tree belongs to
 * @return Depth of extent tree */
static inline u16int
ext4_extent_tree_depth(struct ext4_inode_ref *inode_ref)
{
	struct ext4_extent_header *eh;
	eh = ext4_inode_get_extent_header(inode_ref->inode);
	return ext4_extent_header_get_depth(eh);
}

static struct ext4_extent_tail *
ext4_extent_get_csum_tail(struct ext4_extent_header *eh)
{
	return (struct ext4_extent_tail *)(((char *)eh) +
	    EXT4_EXTENT_TAIL_OFFSET(eh));
}

static u32int ext4_extent_block_csum(struct ext4_inode_ref *inode_ref,
				       struct ext4_extent_header *eh)
{
	u32int checksum = 0;
	struct ext4_sblock *sb = &inode_ref->fs->sb;

	if (ext4_sb_feature_ro_com(sb, EXT4_FRO_COM_METADATA_CSUM)) {
		u32int ino_index = to_le32(inode_ref->index);
		u32int ino_gen =
			to_le32(ext4_inode_get_generation(inode_ref->inode));
		/* First calculate crc32 checksum against fs uuid */
		checksum = inode_ref->fs->uuid_crc32c;
		/* Then calculate crc32 checksum against inode number
		 * and inode generation */
		checksum = ext4_crc32_u(checksum, ino_index);
		checksum = ext4_crc32_u(checksum, ino_gen);
		/* Finally calculate crc32 checksum against
		 * the entire extent block up to the checksum field */
		checksum = ext4_crc32c(checksum, eh, EXT4_EXTENT_TAIL_OFFSET(eh));
	}
	return checksum;
}

static bool
ext4_extent_verify_block_csum(struct ext4_inode_ref *inode_ref,
			      struct ext4_block *block)
{
	u16int rootdepth;
	struct ext4_extent_tail *tail;
	struct ext4_extent_header *eh;

	rootdepth = ext4_extent_tree_depth(inode_ref);

	if (!ext4_sb_feature_ro_com(&inode_ref->fs->sb,
				    EXT4_FRO_COM_METADATA_CSUM))
		return true;

	eh = (struct ext4_extent_header *)block->data;
	if (ext4_extent_header_get_depth(eh) < rootdepth) {
		tail = ext4_extent_get_csum_tail(eh);
		return tail->checksum ==
		    to_le32(ext4_extent_block_csum(inode_ref, eh));
	}

	return true;
}

static void
ext4_extent_block_csum_set(struct ext4_inode_ref *inode_ref,
			   struct ext4_extent_header *eh)
{
	u16int rootdepth;
	struct ext4_extent_tail *tail;

	rootdepth = ext4_extent_tree_depth(inode_ref);

	if (!ext4_sb_feature_ro_com(&inode_ref->fs->sb,
				    EXT4_FRO_COM_METADATA_CSUM))
		return;

	if (ext4_extent_header_get_depth(eh) < rootdepth) {
		tail = ext4_extent_get_csum_tail(eh);
		tail->checksum = to_le32(ext4_extent_block_csum(inode_ref, eh));
	}
}

#ifdef CONFIG_EXTENT_DEBUG_VERBOSE
static void
ext4_extent_print_path(struct ext4_inode_ref *inode_ref,
		       struct ext4_extent_path *path)
{
	u16int rootdepth;
	struct ext4_extent_path *p;

	rootdepth = ext4_extent_tree_depth(inode_ref);
	p = path + rootdepth;

	ext4_dbg(DEBUG_EXTENT,
		 DBG_INFO "Path address: %p\n", path);
	while (p >= path) {
		u16int i;
		u16int entries =
		    ext4_extent_header_get_nentries(p->header);
		u16int limit =
		    ext4_extent_header_get_max_nentries(p->header);

		ext4_dbg(DEBUG_EXTENT,
DBG_INFO "-- Block: %llud, Depth: %uhd, Entries: %uhd, Limit: %uhd\n",
			 p->block.lb_id, p->depth, entries, limit);
		for (i = 0; i < entries; i++) {
			if (p->depth) {
				struct ext4_extent_index *index;

				index = EXT4_EXTENT_FIRST_INDEX(p->header) + i;
				ext4_dbg(DEBUG_EXTENT,
DBG_INFO "Index: iblock: %ud, fsblock: %llud\n",
					 ext4_extent_index_get_iblock(index),
					 ext4_extent_index_get_fblock(index));
			} else {
				struct ext4_extent *extent;

				extent = EXT4_EXTENT_FIRST(p->header) + i;
				ext4_dbg(DEBUG_EXTENT,
DBG_INFO "Extent: iblock: %ud, fsblock: %llud, count: %uhd\n",
					 ext4_extent_get_iblock(extent),
					 ext4_extent_get_fblock(extent),
					 ext4_extent_get_nblocks(extent));
			}
		}

		p--;
	}

	ext4_dbg(DEBUG_EXTENT,
		 DBG_INFO "====================\n");
}
#else /* CONFIG_EXTENT_DEBUG_VERBOSE */
#define ext4_extent_print_path(...)
#endif /* CONFIG_EXTENT_DEBUG_VERBOSE */

/**@brief Binary search in extent index node.
 * @param header Extent header of index node
 * @param index  Output value - found index will be set here
 * @param iblock Logical block number to find in index node */
static void ext4_extent_binsearch_idx(struct ext4_extent_header *header,
				      struct ext4_extent_index **index,
				      ext4_lblk_t iblock)
{
	struct ext4_extent_index *r;
	struct ext4_extent_index *l;
	struct ext4_extent_index *m;

	u16int nentries = ext4_extent_header_get_nentries(header);

	/* Initialize bounds */
	l = EXT4_EXTENT_FIRST_INDEX(header) + 1;
	r = EXT4_EXTENT_FIRST_INDEX(header) + nentries - 1;

	/* Do binary search */
	while (l <= r) {
		m = l + (r - l) / 2;
		ext4_lblk_t eiiblock = ext4_extent_index_get_iblock(m);

		if (iblock < eiiblock)
			r = m - 1;
		else
			l = m + 1;
	}

	/* Set output value */
	*index = l - 1;
}

/**@brief Binary search in extent leaf node.
 * @param header Extent header of leaf node
 * @param extent Output value - found extent will be set here,
 *               or nil if node is empty
 * @param iblock Logical block number to find in leaf node */
static void ext4_extent_binsearch(struct ext4_extent_header *header,
				  struct ext4_extent **extent,
				  ext4_lblk_t iblock)
{
	struct ext4_extent *r;
	struct ext4_extent *l;
	struct ext4_extent *m;

	u16int nentries = ext4_extent_header_get_nentries(header);

	if (nentries == 0) {
		/* this leaf is empty */
		*extent = nil;
		return;
	}

	/* Initialize bounds */
	l = EXT4_EXTENT_FIRST(header) + 1;
	r = EXT4_EXTENT_FIRST(header) + nentries - 1;

	/* Do binary search */
	while (l <= r) {
		m = l + (r - l) / 2;
		ext4_lblk_t eiblock = ext4_extent_get_iblock(m);

		if (iblock < eiblock)
			r = m - 1;
		else
			l = m + 1;
	}

	/* Set output value */
	*extent = l - 1;
}

static void
ext4_extent_path_dirty(struct ext4_inode_ref *inode_ref,
		       struct ext4_extent_path *path,
		       u16int depth)
{
	u16int rootdepth;
	rootdepth = ext4_extent_tree_depth(inode_ref);

	if (rootdepth != depth) {
		struct ext4_extent_path *p;
		p = path + depth;
		ext4_extent_block_csum_set(inode_ref, p->header);
		ext4_trans_set_block_dirty(p->block.buf);
	} else
		inode_ref->dirty = true;
}

static int
ext4_extent_path_release(struct ext4_inode_ref *inode_ref,
			 struct ext4_extent_path *path)
{
	int ret = 0;
	u16int i, rootdepth;

	rootdepth = ext4_extent_tree_depth(inode_ref);

	for (i = 0; i < rootdepth; i++) {
		if (path[i].block.lb_id) {
			ret = ext4_block_set(inode_ref->fs->bdev,
					     &path[i].block);
			if (ret != 0)
				break;
		}
	}

	return ret;
}

/**@brief Physical block allocation hint for extent tree manipulation
 * routines
 * @param inode_ref I-node
 * @return Physical block allocation hint */
static ext4_fsblk_t
ext4_extent_tree_alloc_goal(struct ext4_inode_ref *inode_ref)
{
	u32int bgid;
	struct ext4_sblock *sb;

	sb = &inode_ref->fs->sb;
	bgid = inode_ref->index / ext4_get32(sb, inodes_per_group);

	/* Currently for allocations from extent tree manipulation routines,
	 * we try the blocks in the block group the inode table block refers
	 * to */
	return ext4_fs_first_bg_block_no(sb, bgid);
}

/**@brief Physical block allocation hint for data blocks routines
 * @param inode_ref I-node
 * @param path      path in the extent tree
 * @param iblock    the starting logical block of the
 * mapping to be inserted
 * @return Physical block allocation hint */
static ext4_fsblk_t
ext4_extent_data_alloc_goal(struct ext4_inode_ref *inode_ref,
			    struct ext4_extent_path *path,
			    ext4_lblk_t iblock)
{
	ext4_fsblk_t ret;
	struct ext4_extent *ext;

	ext = path[0].extent;
	if (!ext)
		/* If there is no mapping yet, we return
		 * ext4_extent_tree_alloc_goal() as hints */
		return ext4_extent_tree_alloc_goal(inode_ref) + iblock;

	/* We want the whole file to be continuous. */
	if (ext4_extent_get_iblock(ext) < iblock)
		ret = ext4_extent_get_fblock(ext) +
		    iblock - ext4_extent_get_iblock(ext);
	else {
		if (ext4_extent_get_iblock(ext) - iblock >
		    ext4_extent_get_fblock(ext))
			ret = ext4_extent_get_fblock(ext);
		else
			ret = ext4_extent_get_fblock(ext) -
			    (ext4_extent_get_iblock(ext) - iblock);
	}

	return ret;
}

/**@brief Verify the extent node block is valid
 * @param inode_ref I-node
 * @param block     block buffer of the extent node block
 * @param depth     the depth of extent node wanted
 * @return true if the block passes verification, otherwise false
 */
static bool ext4_extent_block_verify(struct ext4_inode_ref *inode_ref,
				     struct ext4_block *block,
				     u16int depth)
{
	u32int blocksz;
	u16int maxnentries;
	struct ext4_extent_header *eh;

	eh = (struct ext4_extent_header *)block->data;
	blocksz = ext4_sb_get_block_size(&inode_ref->fs->sb);

	/* Check if the magic number of the extent node header is correct */
	if (ext4_extent_header_get_magic(eh) != EXT4_EXTENT_MAGIC) {
		ext4_dbg(DEBUG_EXTENT,
DBG_ERROR "Extent node block header mismatch! Block number: %llud\n",
			 block->lb_id);
		return false;
	}

	/* Check if the depth field of extent node header matches what the
	 * caller wants */
	if (ext4_extent_header_get_depth(eh) != depth) {
		ext4_dbg(DEBUG_EXTENT,
DBG_ERROR "Extent node block depth mismatch! Expected: %uhd, Got: %uhd. Block number: %llud\n",
			 depth, ext4_extent_header_get_depth(eh),
			 block->lb_id);
		return false;
	}

	/* Check if the non-root node contains entries */
	if (!ext4_extent_header_get_nentries(eh)) {
		ext4_dbg(DEBUG_EXTENT,
DBG_ERROR "Extent node block does not contain any entries! Block number: %llud\n",
			 block->lb_id);
		return false;
	}

	/* Make sure that the maximum entries field of the
	 * extent node header is correct */
	maxnentries = (blocksz - sizeof(struct ext4_extent_header)) /
	    sizeof(struct ext4_extent);

	if (ext4_extent_header_get_max_nentries(eh) != maxnentries) {
		ext4_dbg(DEBUG_EXTENT,
DBG_ERROR "Incorrect extent node block maximum entries field! Expected: %uhd, Got: %uhd. Block number: %llud\n",
			 maxnentries,
			 ext4_extent_header_get_max_nentries(eh),
			 block->lb_id);
		return false;
	}

	/* Check if the checksum of the block is correct */
	if (!ext4_extent_verify_block_csum(inode_ref,
					   block)) {
		ext4_dbg(DEBUG_EXTENT,
DBG_ERROR "Extent node block checksum failed! Block number: %llud\n",
			 block->lb_id);
		return false;
	}

	/* The block passes verification */
	return true;
}

/**@brief Find extent for specified iblock.
 * This function is used for finding block in the extent tree with
 * saving the path through the tree for possible future modifications.
 * @param inode_ref I-node to read extent tree from
 * @param iblock    Iblock to find extent for
 * @param ppath  Output value - loaded path from extent tree
 * @return Error code */
static int ext4_extent_find_extent(struct ext4_inode_ref *inode_ref,
				   ext4_lblk_t iblock,
				   struct ext4_extent_path **ppath)
{
	struct ext4_extent_header *eh;
	int ret;
	u16int depth;
	u16int k;
	struct ext4_extent_path *tpath;

	depth = ext4_extent_tree_depth(inode_ref);
	eh = ext4_inode_get_extent_header(inode_ref->inode);

	/* Added 2 for possible tree growing (1 extra depth) */
	tpath = ext4_malloc(sizeof(struct ext4_extent_path) * (depth + 2));
	if (tpath == nil) {
		werrstr(Enomem);
		return -1;
	}

	/* Zero the path array because we need to make sure that
	 * lb_id field of block buffer is zero */
	memset(tpath, 0, sizeof(struct ext4_extent_path) * (depth + 2));

	/* Initialize structure for algorithm start */
	k = depth;
	tpath[k].block = inode_ref->block;
	tpath[k].header = eh;

	/* Walk through the extent tree */
	while ((depth = ext4_extent_header_get_depth(eh)) != 0) {
		/* Search index in index node by iblock */
		ext4_extent_binsearch_idx(tpath[k].header,
					  &tpath[k].index, iblock);

		tpath[k].depth = depth;
		tpath[k].extent = nil;

		assert(tpath[k].index != 0);

		/* Load information for the next iteration */
		u64int fblock =
		    ext4_extent_index_get_fblock(tpath[k].index);

		struct ext4_block block;
		ret = ext4_trans_block_get(inode_ref->fs->bdev, &block, fblock);
		if (ret != 0)
			goto errout0;

		if (!ext4_extent_block_verify(inode_ref, &block, depth - 1)) {
			werrstr(Eio);
			ret = -1;
			goto errout0;
		}

		k--;

		eh = (struct ext4_extent_header *)block.data;
		tpath[k].block = block;
		tpath[k].header = eh;
	}

	tpath[k].depth = 0;
	tpath[k].extent = nil;
	tpath[k].index = nil;

	/* Find extent in the leaf node */
	ext4_extent_binsearch(tpath[k].header, &tpath[k].extent,
			      iblock);
	*ppath = tpath;

	return 0;

errout0:
	/* Put loaded blocks */
	ext4_extent_path_release(inode_ref, tpath);

	/* Destroy temporary data structure */
	ext4_free(tpath);

	return ret;
}

/**@brief Reload the paths in a cursor starting from the level having invalid
 * pointer
 * @param inode_ref I-node the extent tree resides in
 * @param path      Path in the extent tree
 * @param depth     The level to start the reload at
 * @param right     Try to load the rightmost children
 * @return 0 on success, Eio on corrupted block, or return values of
 * ext4_trans_block_get(). */
int ext4_extent_reload_paths(struct ext4_inode_ref *inode_ref,
			     struct ext4_extent_path *path,
			     u16int depth,
			     bool right)
{
	int ret = 0;
	struct ext4_extent_header *header;
	struct ext4_extent_path *p;

	/* actually we assume our caller starting from index level instead of
	 * extent level */
	assert(depth);

	p = path + depth;
	header = p->header;

	/* XXX: the path becomes invalid at the first place... */
	if (p->index > EXT4_EXTENT_LAST_INDEX(header))
		p->index = EXT4_EXTENT_LAST_INDEX(header);

	/* Start reloading all the paths from the child of the specified level
	 * toward the leaf */
	for (; p > path; --p) {
		struct ext4_extent_path *chldp;
		struct ext4_extent_index *idx;

		chldp = p - 1;
		header = p->header; USED(header);
		idx = p->index;

		/* Release the buffer of child path if the buffer is still
		 * valid */
		if (chldp->block.lb_id) {
			ret = ext4_block_set(inode_ref->fs->bdev, &chldp->block);
			if (ret != 0)
				goto out;
		}

		/* Read the block specified by the physical block field of the
		 * index */
		ret = ext4_trans_block_get(inode_ref->fs->bdev, &chldp->block,
					   ext4_extent_index_get_fblock(idx));
		if (ret != 0)
			goto out;

		header = (struct ext4_extent_header *)chldp->block.data;
		/* Validate the block content before moving on. */
		if (!ext4_extent_block_verify(inode_ref,
					      &chldp->block, p->depth - 1)) {
			werrstr(Eio);
			ret = -1;
			goto out;
		}

		/* Reset the fields of child path */
		chldp->header = header;
		chldp->depth = ext4_extent_header_get_depth(header);
		if (right) {
			if (chldp->depth) {
				chldp->index = EXT4_EXTENT_LAST_INDEX(header);
				chldp->extent = nil;
			} else {
				chldp->extent = EXT4_EXTENT_LAST(header);
				chldp->index = nil;
			}
		} else {
			if (chldp->depth) {
				chldp->index = EXT4_EXTENT_FIRST_INDEX(header);
				chldp->extent = nil;
			} else {
				chldp->extent = EXT4_EXTENT_FIRST(header);
				chldp->index = nil;
			}
		}
	}
out:
	return ret;
}

/**@brief Seek to the next extent
 * @param inode_ref I-node the extent tree resides in
 * @param path      Path in the extent tree
 * @param nonextp   Output value - whether the current extent is the
 * right-most extent already
 * @return 0 on success, Eio on currupted block, or return values of
 * ext4_trans_block_get(). */
int ext4_extent_increment(struct ext4_inode_ref *inode_ref,
			  struct ext4_extent_path *path,
			  bool *nonextp)
{
	int ret = 0;
	u16int ptr;
	bool nonext = true;
	u16int depth = 0;
	struct ext4_extent_path *p;
	u16int rootdepth;

	p = path;
	rootdepth = ext4_extent_tree_depth(inode_ref);

	/* Iterate the paths from the leaf to the root */
	while (depth <= rootdepth) {
		struct ext4_extent_header *header;

		if (p->depth) {
			ptr = p->index -
			    EXT4_EXTENT_FIRST_INDEX(p->header);
		} else {
			ptr = p->extent -
			    EXT4_EXTENT_FIRST(p->header);
		}

		header = p->header;

		if (ptr < ext4_extent_header_get_nentries(header) - 1)
			/* We found a path with non-rightmost pointer */
			break;

		/* Move to the parent path */
		p++;
		depth++;
	}

	/* If we can't find a path with a non-rightmost pointer,
	 * we are already on the last extent, just return in this
	 * case */
	if (depth > rootdepth)
		goto out;

	/* Increment the pointer once we found a path with non-rightmost
	 * pointer */
	if (p->depth)
		p->index++;
	else
		p->extent++;

	if (depth) {
		/* We need to reload the paths to leaf if the path iterator
		 * is not pointing to the leaf */
		ret = ext4_extent_reload_paths(inode_ref, path, depth, false);
		if (ret != 0)
			goto out;
	}

	/* Found the next extent */
	nonext = false;
out:
	if (nonextp)
		*nonextp = nonext;

	return ret;
}

/**@brief Seek to the previous extent
 * @param inode_ref I-node the extent tree resides in
 * @param path      Path in the extent tree
 * @param noprevp   Output value - whether the current extent is the
 * left-most extent already
 * @return 0 on success, Eio on currupted block, or return values of
 * ext4_trans_block_get(). */
int
ext4_extent_decrement(struct ext4_inode_ref *inode_ref,
		      struct ext4_extent_path *path,
		      bool *noprevp)
{
	int ret = 0;
	u16int ptr;
	bool noprev = true;
	u16int depth = 0;
	struct ext4_extent_path *p;
	u16int rootdepth;

	p = path;
	rootdepth = ext4_extent_tree_depth(inode_ref);

	/* Iterate the paths from the leaf to the root */
	while (depth <= rootdepth) {
		if (p->depth) {
			ptr = p->index -
			    EXT4_EXTENT_FIRST_INDEX(p->header);
		} else {
			ptr = p->extent -
			    EXT4_EXTENT_FIRST(p->header);
		}

		if (ptr)
			/* We found a path with non-leftmost pointer */
			break;

		/* Move to the parent path */
		p++;
		depth++;
	}

	/* If we can't find a path with a non-leftmost pointer,
	 * we are already on the first extent, just return in this
	 * case */
	if (depth > rootdepth)
		goto out;

	/* Decrement the pointer once we found a path with non-leftmost
	 * pointer */
	if (p->depth)
		p->index--;
	else
		p->extent--;

	if (depth) {
		/* We need to reload the paths to leaf if the path iterator
		 * is not pointing to the leaf */
		ret = ext4_extent_reload_paths(inode_ref, path, depth, true);
		if (ret != 0)
			goto out;
	}

	/* Found the previous extent */
	noprev = false;
out:
	if (noprevp)
		*noprevp = noprev;
	return ret;
}


/**@brief Update the index of nodes starting from leaf
 * @param inode_ref I-node the extent tree resides in
 * @param path      Path in the extent tree
 * @param force     set this to true if insertion, deletion or modification
 * of starting logical block of the first index in a node is made at non-leaf
 * level */
static void ext4_extent_update_index(struct ext4_inode_ref *inode_ref,
				     struct ext4_extent_path *path,
				     bool force)
{
	u16int rootdepth;
	struct ext4_extent_path *p;

	rootdepth = ext4_extent_tree_depth(inode_ref);

	/* Iterate the paths from the parent of the leaf to the root */
	for (p = path + 1; p <= path + rootdepth; p++) {
		struct ext4_extent_path *chldp;
		struct ext4_extent_header *child_header;
		intptr chldptr;

		/* This points to the child path of the current path */
		chldp = p - 1;
		child_header = chldp->header;

		if (!chldp->depth)
			chldptr = chldp->extent -
				    EXT4_EXTENT_FIRST(child_header);
		else
			chldptr = chldp->index -
			            EXT4_EXTENT_FIRST_INDEX(child_header);

		/* If the modification on the child node is not made on the
		 * first slot of the node, we are done */
		if (chldptr)
			break;

		if (p->depth > 1) {
			struct ext4_extent_index *idx = p->index;
			struct ext4_extent_index *chldidx =
					chldp->index;
			ext4_lblk_t iblock, chldiblock;

			iblock = ext4_extent_index_get_iblock(idx);
			chldiblock = ext4_extent_index_get_iblock(chldidx);

			if (iblock != chldiblock) {
				/* If the starting logical block of the first
				 * index of the child node is modified, we
				 * update the starting logical block of index
				 * pointing to the child node */
				ext4_extent_index_set_iblock(idx, chldiblock);
				ext4_extent_path_dirty(inode_ref, path,
						       p->depth);
			} else if (!force)
				/* We do not need to continue the iteration */
				break;
		} else {
			struct ext4_extent_index *idx = p->index;
			struct ext4_extent *chldext = chldp->extent;
			ext4_lblk_t iblock, chldiblock;

			iblock = ext4_extent_index_get_iblock(idx);
			chldiblock = ext4_extent_get_iblock(chldext);

			if (iblock != chldiblock) {
				/* If the starting logical block of the first
				 * extent of the child node is modified, we
				 * update the starting logical block of index
				 * pointing to the child node */
				ext4_extent_index_set_iblock(idx, chldiblock);
				ext4_extent_path_dirty(inode_ref, path,
						       p->depth);
			} else if (!force)
				/* We do not need to continue the iteration */
				break;
		}
	};
}

/**@brief Make the tree grow up by one level
 * @param inode_ref  I-node the extent tree resides in
 * @param path       Path in the extent tree
 * @param new_fblock The newly allocated block for tree growth
 * @return Error code */
static int ext4_extent_grow_tree(struct ext4_inode_ref *inode_ref,
				 struct ext4_extent_path *path,
				 ext4_fsblk_t newfblock)
{
	int rc;
	u16int ptr;
	struct ext4_block block;
	ext4_lblk_t chldiblock;
	u16int rootdepth;
	struct ext4_block rootblock;
	struct ext4_extent_header *rooteh;
	struct ext4_extent_path *nrootp;
	struct ext4_extent_path *rootp;
	u32int blocksz;
	u16int maxnentries;

	rootdepth = ext4_extent_tree_depth(inode_ref);
	rootp = path + rootdepth;
	nrootp = rootp + 1;
	rootblock = rootp->block;
	rooteh = rootp->header;
	blocksz = ext4_sb_get_block_size(&inode_ref->fs->sb);

	/* Store the extent/index offset so that we can recover the
	 * pointer to it later */
	if (rootdepth) {
		ptr = rootp->index -
		    EXT4_EXTENT_FIRST_INDEX(rootp->header);
	} else {
		ptr = rootp->extent -
		    EXT4_EXTENT_FIRST(rootp->header);
	}
	/* Prepare a buffer for newly allocated block */
	rc = ext4_trans_block_get_noread(inode_ref->fs->bdev, &block, newfblock);
	if (rc != 0)
		return rc;

	/* Initialize newly allocated block */
	memset(block.data, 0, blocksz);

	/* Move data from root to the new block */
	memcpy(block.data, inode_ref->inode->blocks,
	       EXT4_INODE_BLOCKS * sizeof(u32int));

	/* Update old root path */
	rootp->block = block;
	rootp->header = (struct ext4_extent_header *)block.data;
	if (rootp->depth) {
		rootp->index =
		    EXT4_EXTENT_FIRST_INDEX(rootp->header) +
		    ptr;

		maxnentries =
		    (blocksz - sizeof(struct ext4_extent_header)) /
		    sizeof(struct ext4_extent_index);
		rootp->extent = nil;
		chldiblock =
		    ext4_extent_index_get_iblock(EXT4_EXTENT_FIRST_INDEX(rootp->header));
	} else {
		rootp->extent =
			EXT4_EXTENT_FIRST(rootp->header) +
			ptr;
		maxnentries =
		    (blocksz - sizeof(struct ext4_extent_header)) /
		    sizeof(struct ext4_extent);
		rootp->index = nil;
		chldiblock =
			ext4_extent_get_iblock(EXT4_EXTENT_FIRST(rootp->header));
	}

	/* Re-initialize new root metadata */
	nrootp->depth = rootdepth + 1;
	nrootp->block = rootblock;
	nrootp->header = rooteh;
	nrootp->extent = nil;
	nrootp->index = EXT4_EXTENT_FIRST_INDEX(nrootp->header);

	ext4_extent_header_set_depth(nrootp->header, nrootp->depth);

	/* Create new entry in root */
	ext4_extent_header_set_nentries(nrootp->header, 1);
	ext4_extent_index_set_iblock(nrootp->index, chldiblock);
	ext4_extent_index_set_fblock(nrootp->index, newfblock);

	/* Since new_root belongs to on-disk inode,
	 * we don't do checksum here */
	inode_ref->dirty = true;

	/* Set upper limit for entries count of old root */
	ext4_extent_header_set_max_nentries(rootp->header, maxnentries);

	ext4_extent_path_dirty(inode_ref, path, rootp->depth);

	return 0;
}

/**@brief Do splitting on the tree if the leaf is full
 * @param inode_ref I-node the extent tree resides in
 * @param path      Path in the extent tree for possible splitting
 * @param nslots    number of entries that will be inserted to the
 * leaf in future.
 * @return Error code */
static int ext4_extent_split(struct ext4_inode_ref *inode_ref,
			     struct ext4_extent_path *path,
			     u16int nslots)
{
	int ret;
	u16int i;
	ext4_fsblk_t goal;
	u16int rootdepth;
	struct ext4_extent_path *p;
	u32int blocksz;
	/* Number of new blocks to be allocated */
	u16int nnewfblocks = 0;
	/* Number of node to be split */
	u16int nsplits = 0;
	/* Array of new blocks allocated */
	ext4_fsblk_t *newfblocks;
	/* The index of the right block inserted last time */
	ext4_lblk_t lastiblock = 0;
	/* Whether we updated child path to point to the right block
	 * at the previous round during splitting */
	bool prevrblock = false;

	blocksz = ext4_sb_get_block_size(&inode_ref->fs->sb);
	rootdepth = ext4_extent_tree_depth(inode_ref);
	goal = ext4_extent_tree_alloc_goal(inode_ref);

	/* First calculate how many levels will be touched */
	for (p = path; p <= path + rootdepth; p++) {
		u16int entries =
		    ext4_extent_header_get_nentries(p->header);
		u16int limit =
		    ext4_extent_header_get_max_nentries(p->header);

		assert(entries <= limit);
		if (!p->depth) {
			if (entries + nslots <= limit)
				break;
		} else {
			if (entries < limit)
				break;
		}
		/* We have to split a node when the tree is full */
		nnewfblocks++;
		nsplits++;
	}

	if (!nnewfblocks)
		return 0;

	/* Allocate the array for storing newly allocated blocks */
	newfblocks = ext4_malloc(sizeof(ext4_fsblk_t) * nnewfblocks);
	if (!newfblocks) {
		werrstr(Enomem);
		return -1;
	}

	for (i = 0; i < nnewfblocks; i++) {
		ret = ext4_balloc_alloc_block(inode_ref, goal, newfblocks + i);
		if (ret != 0)
			return ret;
	}

	ext4_dbg(DEBUG_EXTENT,
		 DBG_INFO "nnewfblocks: %uhd rootdepth: %uhd\n",
		 nnewfblocks, rootdepth);

	/* If number of blocks to be allocated is greater than
	 * the depth of root we have to grow the tree */
	if (nnewfblocks == rootdepth + 1) {
		ext4_dbg(DEBUG_EXTENT, "Growing: \n");
		nsplits--;

		ret = ext4_extent_grow_tree(inode_ref,
					    path, newfblocks[rootdepth]);
		if (ret != 0)
			goto finish;

		ext4_extent_print_path(inode_ref, path);

		/* If we are moving the in-inode leaf to on-block leaf.
		 * we do not need further actions. */
		if (!rootdepth)
			goto finish;

		++rootdepth; USED(rootdepth);
	}

	/* Start splitting */
	p = path;
	ext4_dbg(DEBUG_EXTENT, DBG_INFO "Start splitting: \n");
	for (i = 0; i < nsplits; i++, p++) {
		struct ext4_extent_header *header;
		u16int entries =
		    ext4_extent_header_get_nentries(p->header);
		u16int limit =
		    ext4_extent_header_get_max_nentries(p->header);
		/* The entry we start shifting to the right block */
		u16int split_ptr = entries / 2;
		/* The number of entry the right block will have */
		u16int right_entries = entries - split_ptr;
		/* The current entry */
		u16int curr_ptr;
		ext4_lblk_t riblock;
		struct ext4_block block;

		ret = ext4_trans_block_get_noread(inode_ref->fs->bdev,
						  &block, newfblocks[i]);
		if (ret != 0)
			goto finish;

		/* Initialize newly allocated block and remember it */
		memset(block.data, 0, blocksz);

		header = (void *)block.data;

		/* Initialize on-disk structure (header) */
		ext4_extent_header_set_nentries(header,
				right_entries);
		ext4_extent_header_set_max_nentries(header, limit);
		ext4_extent_header_set_magic(header, EXT4_EXTENT_MAGIC);
		ext4_extent_header_set_depth(header, p->depth);
		ext4_extent_header_set_generation(header, 0);

		/* Move some entries from old block to new block */
		if (p->depth) {
			struct ext4_extent_index *left_index =
				EXT4_EXTENT_FIRST_INDEX(p->header);
			struct ext4_extent_index *split_index =
				left_index + split_ptr;

			riblock = ext4_extent_index_get_iblock(split_index);
			ext4_dbg(DEBUG_EXTENT,
				 DBG_INFO "depth: %ud, riblock: %ud\n",
				 p->depth, riblock);

			curr_ptr = p->index - left_index;

			memcpy(EXT4_EXTENT_FIRST_INDEX(header),
			       split_index,
			       right_entries * EXT4_EXTENT_INDEX_SIZE);
			memset(split_index, 0,
			       right_entries * EXT4_EXTENT_INDEX_SIZE);
		} else {
			struct ext4_extent *left_extent =
				EXT4_EXTENT_FIRST(p->header);
			struct ext4_extent *split_extent =
				left_extent + split_ptr;

			riblock = ext4_extent_get_iblock(split_extent);
			ext4_dbg(DEBUG_EXTENT,
				 DBG_INFO "depth: %ud, riblock: %ud\n",
				 p->depth, riblock);

			curr_ptr = p->extent - left_extent;

			memcpy(EXT4_EXTENT_FIRST(header),
			       split_extent,
			       right_entries * EXT4_EXTENT_SIZE);
			memset(split_extent, 0,
			       right_entries * EXT4_EXTENT_SIZE);
		}

		/* Set entries count in left node */
		ext4_extent_header_set_nentries(p->header,
						entries - right_entries);

		/* Decide whether we need to update the path to
		 * point to right block or not */
		if (curr_ptr >= split_ptr) {
			/* Update the checksum for the left block */
			ext4_extent_path_dirty(inode_ref, path, p->depth);

			/* Put back the left block */
			ret = ext4_block_set(inode_ref->fs->bdev,
					     &p->block);
			if (ret != 0)
				goto finish;

			/* Update pointers in extent path structure to
			 * point to right block */
			p->block = block;
			p->header = (void *)block.data;

			if (p->depth) {
				p->index =
				    EXT4_EXTENT_FIRST_INDEX(p->header) +
				    curr_ptr - split_ptr;
			} else {
				p->extent =
				    EXT4_EXTENT_FIRST(p->header) +
				    curr_ptr - split_ptr;
			}
		} else {
			/* Update the checksum for the right block */
			ext4_extent_block_csum_set(inode_ref, header);
			ext4_trans_set_block_dirty(block.buf);

			/* Put back the right block */
			ret = ext4_block_set(inode_ref->fs->bdev,
					     &block);
			if (ret != 0)
				goto finish;
		}

		/* Append an index after the current index */
		if (p->depth) {
			struct ext4_extent_index *index = p->index + 1;

			/* If we updated the path to right block in the previous
			 * round, we update the pointer in the path to point to
			 * the right block */
			if (prevrblock)
				p->index++;

			if (index <= EXT4_EXTENT_LAST_INDEX(p->header)) {
				u16int nindex =
					EXT4_EXTENT_LAST_INDEX(p->header) -
					index + 1;

				memmove(index + 1,
					index,
					nindex * EXT4_EXTENT_INDEX_SIZE);
			}
			memset(index, 0, EXT4_EXTENT_INDEX_SIZE);
			ext4_extent_index_set_iblock(index, lastiblock);
			ext4_extent_index_set_fblock(index, newfblocks[i - 1]);

			entries = ext4_extent_header_get_nentries(p->header);
			ext4_extent_header_set_nentries(p->header,
					entries + 1);
		}

		ext4_extent_path_dirty(inode_ref, path, p->depth);

		/* We may have updated the path to right block in this round */
		prevrblock = curr_ptr >= split_ptr;

		/* We also update the lastiblock variable to the index of the
		 * right block */
		lastiblock = riblock;
	}

	/* Append an index after the current index */
	if (p->depth) {
		struct ext4_extent_index *index = p->index + 1;
		u16int entries =
		    ext4_extent_header_get_nentries(p->header);

		/* If we updated the path to right block in the previous
		 * round, we update the pointer in the path to point to
		 * the right block */
		if (prevrblock)
			p->index++;

		if (index <= EXT4_EXTENT_LAST_INDEX(p->header)) {
			u16int nindex =
				EXT4_EXTENT_LAST_INDEX(p->header) -
				index + 1;

			memmove(index + 1,
				index,
				nindex * EXT4_EXTENT_INDEX_SIZE);
		}
		memset(index, 0, EXT4_EXTENT_INDEX_SIZE);
		ext4_extent_index_set_iblock(index, lastiblock);
		ext4_extent_index_set_fblock(index, newfblocks[i - 1]);
		ext4_extent_header_set_nentries(p->header,
				entries + 1);

		ext4_extent_path_dirty(inode_ref, path, p->depth);
	}

	ret = 0;
finish:
	if (ret != 0)
		for (i = 0; i < nnewfblocks; i++)
			ext4_balloc_free_block(inode_ref, newfblocks[i]);

	ext4_free(newfblocks);
	return ret;
}

/**@brief Insert an extent into the extent tree
 * @param inode_ref I-node the extent tree resides in
 * @param path      Path in the extent tree for possible splitting
 * @param ext       Extent to be inserted
 * @return Error code */
static int ext4_extent_insert(struct ext4_inode_ref *inode_ref,
			      struct ext4_extent_path *path,
			      struct ext4_extent *ext)
{
	int ret;
	u16int entries;
	struct ext4_extent_path *p;

	/* Split and grow the tree if necessary */
	ret = ext4_extent_split(inode_ref, path, 1);
	if (ret != 0)
		return ret;

	p = path;
	entries = ext4_extent_header_get_nentries(p->header);

	ext4_dbg(DEBUG_EXTENT, DBG_INFO "After splitting: \n");
	ext4_extent_print_path(inode_ref, path);

	if (!p->extent) {
		p->extent = EXT4_EXTENT_FIRST(p->header);
	} else {
		ext4_lblk_t iblock;

		iblock = ext4_extent_get_iblock(p->extent);
		if (ext4_extent_get_iblock(ext) > iblock)
			p->extent++;
	}

	if (p->extent <= EXT4_EXTENT_LAST(p->header)) {
		u16int nextent =
			EXT4_EXTENT_LAST(p->header) -
			p->extent + 1;

		ext4_dbg(DEBUG_EXTENT,
			 DBG_INFO "%uhd extents to be shifted at leaf\n",
			 nextent);

		memmove(p->extent + 1,
			p->extent,
			nextent * EXT4_EXTENT_SIZE);
	}
	memcpy(p->extent, ext, EXT4_EXTENT_SIZE);
	ext4_extent_header_set_nentries(p->header,
					entries + 1);

	ext4_extent_path_dirty(inode_ref, path, p->depth);

	ext4_dbg(DEBUG_EXTENT, DBG_INFO "Before updating indice: \n");
	ext4_extent_print_path(inode_ref, path);

	/* Update the index of the first entry in parents node */
	ext4_extent_update_index(inode_ref, path, false);

	ext4_dbg(DEBUG_EXTENT, DBG_INFO "At the end: \n");
	ext4_extent_print_path(inode_ref, path);

	return ret;
}

/**@brief Delete an item from the node at @depth pointed
 * @param inode_ref I-node the extent tree resides in
 * @param path      Path in the extent tree for possible splitting
 * @param depth     The level of the node to be operated on
 * @return Error code */
static void
ext4_extent_delete_item(struct ext4_inode_ref *inode_ref,
			struct ext4_extent_path *path,
			u16int depth)
{
	u16int nitems;
	struct ext4_extent_header *hdr;
	struct ext4_extent_path *p;

	p = path + depth;

	hdr = p->header;
	assert(ext4_extent_header_get_nentries(hdr));

	if (p->depth) {
		struct ext4_extent_index *idx;

		idx = p->index;
		nitems = EXT4_EXTENT_LAST_INDEX(hdr) - (idx + 1) + 1;
		if (nitems) {
			memmove(idx, idx + 1,
				nitems * EXT4_EXTENT_INDEX_SIZE);
			memset(EXT4_EXTENT_LAST(hdr), 0,
			       EXT4_EXTENT_INDEX_SIZE);
		} else {
			memset(idx, 0, EXT4_EXTENT_INDEX_SIZE);
		}
	} else {
		struct ext4_extent *ext;

		ext = p->extent;
		nitems = EXT4_EXTENT_LAST(hdr) - (ext + 1) + 1;
		if (nitems) {
			memmove(ext, ext + 1,
				nitems * EXT4_EXTENT_SIZE);
			memset(EXT4_EXTENT_LAST(hdr), 0,
			       EXT4_EXTENT_SIZE);
		} else {
			memset(ext, 0, EXT4_EXTENT_SIZE);
		}
	}

	nitems = ext4_extent_header_get_nentries(hdr) - 1;
	ext4_extent_header_set_nentries(hdr,
					nitems);
	ext4_extent_path_dirty(inode_ref, path, p->depth);
}

/**@brief Remove extents in a leaf starting
 * from the current extent and having
 * key less than or equal to @toiblock.
 * @param inode_ref I-node the tree resides in
 * @param path      Path in the extent tree
 * @param toiblock  The logical block
 * @param stopp     Output value to tell whether the caller should
 * stop deletion. Will be set to true if an extent having key greater
 * than @toiblock is met.
 * @return 0 if there is no error, or return values of blocks
 * freeing routine. */
static int
ext4_extent_delete_leaf(struct ext4_inode_ref *inode_ref,
			struct ext4_extent_path *path,
			ext4_lblk_t toiblock,
			bool *stopp)
{
	int ret = 0;
	u16int nitems;
	struct ext4_extent *ext;
	struct ext4_extent_header *hdr;
	struct ext4_extent_path *p;

	p = path;
	*stopp = false;

	while (1) {
		bool unwritten;
		u16int ptr;
		u16int len;
		u16int flen;
		ext4_lblk_t endiblock;
		ext4_lblk_t startiblock;
		ext4_fsblk_t blocknr;

		hdr = p->header;
		nitems = ext4_extent_header_get_nentries(hdr);
		ptr = p->extent - EXT4_EXTENT_FIRST(hdr);

		assert(nitems > 0);

		ext = p->extent;
		blocknr = ext4_extent_get_fblock(ext);
		startiblock = ext4_extent_get_iblock(ext);
		endiblock = startiblock + ext4_extent_get_nblocks(ext) - 1;
		len = endiblock - startiblock + 1;
		unwritten = EXT4_EXT_IS_UNWRITTEN(ext);

		/* We have to stop if the extent's key
		 * is greater than @toiblock. */
		if (toiblock < startiblock) {
			*stopp = true;
			break;
		}

		if (toiblock < endiblock) {
			/* In case @toiblock is smaller than the last
			 * logical block of the extent, we do not
			 * need to delete the extent. We modify it only. */

			/* Unmap the underlying blocks. */
			flen = toiblock - startiblock + 1;
			ext4_dbg(DEBUG_EXTENT,
				 DBG_INFO "Freeing: %llud:%uhd\n",
				 blocknr, flen);
			ext4_balloc_free_blocks(inode_ref, blocknr, flen);

			/* Adjust the starting block and length of the
			 * current extent. */
			blocknr += flen;
			startiblock = toiblock + 1;
			len = endiblock - startiblock + 1;
			ext4_extent_set_iblock(ext, startiblock);
			ext4_extent_set_nblocks(ext, len, unwritten);
			ext4_extent_set_fblock(ext, blocknr);

			ext4_extent_path_dirty(inode_ref, path, p->depth);

			*stopp = 1;
			break;
		}

		/* Delete the extent pointed to by the path. */
		ext4_extent_delete_item(inode_ref, path, 0);
		nitems--;

		/* Unmap the underlying blocks. */
		flen = len;
		ext4_dbg(DEBUG_EXTENT,
			 DBG_INFO "Freeing: %llud:%uhd\n",
			 blocknr, flen);
		ext4_balloc_free_blocks(inode_ref, blocknr, flen);

		/* There are no more items we could delete. */
		if (ptr >= nitems)
			break;
	}
	return ret;
}

/**@brief Remove the current index at specified level.
 * @param cur   Cursor to an extent tree
 * @param depth The level where deletion takes place at
 * @return 0 if there is no error, or return values of blocks
 * freeing routine. */
static int
ext4_extent_delete_node(struct ext4_inode_ref *inode_ref,
			struct ext4_extent_path *path,
			u16int depth)
{
	int ret = 0;
	ext4_fsblk_t fblock;
	struct ext4_extent_index *idx;
	struct ext4_extent_header *hdr;
	struct ext4_extent_path *p;

	/* If we leave nothing in the node after deletion of
	 * an item, we free the block and delete the index
	 * of the node. Get the respective key of the node
	 * in the parent level */
	p = path + depth;
	hdr = p->header;
	assert(ext4_extent_header_get_nentries(hdr) > 0);
	idx = p->index;
	fblock = ext4_extent_index_get_fblock(idx);

	/* Delete the index pointed to by the path. */
	ext4_extent_delete_item(inode_ref, path, depth);

	/* Free the block of it. */
	ext4_dbg(DEBUG_EXTENT,
		 DBG_INFO "Freeing: %llud:%uhd\n",
		 fblock, 1);
	ext4_balloc_free_blocks(inode_ref, fblock, 1);

	return ret;
}

/**@brief Delete the mapping in extent tree starting from \p fromiblock to
 * \p toiblock inclusively.
 * @param cur Cursor to an extent tree
 * @return 0 on success, ENOENT if there is no item to be deleted,
 * return values of ext4_ext_increment(), ext4_ext_insert(),
 * ext4_ext_delete_leaf(), ext4_ext_delete_node() ext4_ext_reload_paths(),
 * ext4_ext_tree_shrink(). Cursor MUST be discarded after deletion.
 */
int ext4_extent_remove_space(struct ext4_inode_ref *inode_ref,
			     ext4_lblk_t fromiblock,
			     ext4_lblk_t toiblock)
{
	int ret;
	u16int nitems;
	int rootdepth;
	struct ext4_extent_header *hdr;
	struct ext4_extent *ext;
	ext4_lblk_t endiblock;
	ext4_lblk_t startiblock;
	struct ext4_extent_path *path, *p;

	rootdepth = ext4_extent_tree_depth(inode_ref);

	ret = ext4_extent_find_extent(inode_ref, fromiblock, &path);
	if (ret != 0)
		return ret;

	p = path;
	hdr = p->header; USED(hdr);

	/* We return 0 even if the whole extent tree is empty. */
	if (!ext4_extent_header_get_nentries(path->header))
		goto out;

	/* Calculate the last logical block of the current extent. */
	ext4_dbg(DEBUG_EXTENT, DBG_INFO "At start of remove_space: \n");
	ext4_extent_print_path(inode_ref, path);

	ext = p->extent;
	startiblock = ext4_extent_get_iblock(ext);
	endiblock = startiblock + ext4_extent_get_nblocks(ext) - 1;

	ext4_dbg(DEBUG_EXTENT,
		 DBG_INFO "Extent: %ud:%uhd\n",
		 startiblock, endiblock);

	if (fromiblock > endiblock) {
		bool nonext;

		/* The last logical block of the current extent is smaller
		 * than the first logical block we are going to remove,
		 * thus we increment the extent pointer of the cursor. */

		/* Increment the extent pointer to point to the
		 * next extent. */
		ret = ext4_extent_increment(inode_ref, path, &nonext);
		if (ret != 0)
			goto out;

		/* The current extent is already the last extent in
		 * the tree, so we just return success here. */
		if (nonext)
			goto out;
	} else if (fromiblock > startiblock) {
		bool unwritten;
		u16int len;

		/* @fromiblock is in the range of the current extent,
		 * but does not sit right on the starting block.
		 *
		 * In this case we need to modify the current extent.
		 * and free some blocks, since we do not really want
		 * to remove and reinsert a new one. */

		len = fromiblock - startiblock;
		unwritten = EXT4_EXT_IS_UNWRITTEN(ext);
		ext4_extent_set_nblocks(ext, len, unwritten);

		ext4_extent_path_dirty(inode_ref, path, p->depth);

		/* Free the range of blocks starting from @fromiblock
		 * up to either @endiblock or @toiblock. */
		if (toiblock < endiblock) {
			u16int flen;
			ext4_fsblk_t blocknr;
			struct ext4_extent next;

			/* In case we free up space inside an extent
			 * while not touching both ends, we need to
			 * unavoidably insert a new extent right after
			 * the modified current extent, and that may
			 * cause tree splitting. */

			/* Now we need to free up space first. */
			flen = toiblock - fromiblock + 1;
			blocknr = ext4_extent_get_fblock(ext) + len;
			ext4_dbg(DEBUG_EXTENT,
				 DBG_INFO "Freeing: %llud:%uhd\n",
				 blocknr, flen);
			ext4_balloc_free_blocks(inode_ref, blocknr, flen);

			blocknr += flen;
			startiblock = fromiblock + flen;
			len = endiblock - startiblock + 1;

			ext4_extent_set_iblock(&next, startiblock);
			ext4_extent_set_nblocks(&next, len, unwritten);
			ext4_extent_set_fblock(&next, blocknr);
			ret = ext4_extent_insert(inode_ref, path, &next);

			/* After we free up the space and insert a new
			 * extent, we are done. */
			goto out;
		} else {
			bool nonext;
			u16int flen;
			ext4_fsblk_t blocknr;

			/* Otherwise we do not need any insertion,
			 * which also means that no extra space may be
			 * allocated for tree splitting. */
			flen = endiblock - fromiblock + 1;
			blocknr = ext4_extent_get_fblock(ext) + len;

			/* Now we need to free up space first. */
			ext4_dbg(DEBUG_EXTENT,
				 DBG_INFO "Freeing: %llud:%uhd\n",
				 blocknr, flen);
			ext4_balloc_free_blocks(inode_ref, blocknr, flen);

			/* Increment the extent pointer to point to the
			 * next extent. */
			ret = ext4_extent_increment(inode_ref, path, &nonext);
			if (ret != 0 || nonext)
				goto out;
		}
	}

	while (p <= path + rootdepth) {
		struct ext4_extent_path *chldp;

		hdr = p->header;

		if (!p->depth) {
			bool stop;

			/* Delete as much extents as we can. */
			ret = ext4_extent_delete_leaf(inode_ref,
						      path,
						      toiblock,
						      &stop);
			if (ret != 0)
				goto out;

			if (stop) {
				/* Since the current extent has its logical
				 * block number greater than @toiblock,
				 * we are done. */
				break;
			}
			/* Since there are no more items in the leaf,
			 * we have to go one level above to switch to the
			 * next leaf. */
			p++;
			continue;
		}

		chldp = p - 1;
		nitems = ext4_extent_header_get_nentries(chldp->header);

		/* Now we don't need the children path anymore. */
		ext4_block_set(inode_ref->fs->bdev, &chldp->block);
		if (!nitems) {
			ret = ext4_extent_delete_node(inode_ref, path, p->depth);
			if (ret != 0)
				goto out;

			if (p->index > EXT4_EXTENT_LAST_INDEX(hdr)) {
				/* Go one level above */
				p++;
			} else {
				ret = ext4_extent_reload_paths(inode_ref, path, p->depth, false);
				if (ret != 0)
					goto out;
				/* Go to the bottom level (aka the leaf). */
				p = path;
			}
		} else {
			if (p->index == EXT4_EXTENT_LAST_INDEX(hdr)) {
				/* Go one level above */
				p++;
			} else {
				p->index++;
				ret = ext4_extent_reload_paths(inode_ref, path, p->depth, false);
				if (ret != 0)
					goto out;
				/* Go to the bottom level (aka the leaf). */
				p = path;
			}
		}
	}

	/* The above code can only exit in either situations:
	 *
	 * 1. We found that there is no more extents at the right
	 *    (p < path)
	 * 2. We found that the next extent has key larger than @toiblock
	 *    (p at leaf) */
	assert(p == path || p > path + rootdepth);
	if (p == path) {
		/* We might have removed the leftmost key in the node,
		 * so we need to update the first key of the right
		 * sibling at every level until we meet a non-leftmost
		 * key. */
		ext4_extent_update_index(inode_ref, path, true);
	} else {
		/* Put loaded blocks. We won't double-release
		 * in this case since the depth of tree will
		 * be reset to 0. */
		ext4_extent_path_release(inode_ref, path);

		hdr = ext4_inode_get_extent_header(inode_ref->inode);
		if (!ext4_extent_header_get_nentries(hdr)) {
			/* For empty root we need to make sure that the
			 * depth of the root level is 0. */
			ext4_extent_header_set_nentries(hdr, 0);
			ext4_extent_header_set_depth(hdr, 0);
			inode_ref->dirty = true;
		}
	}

out:
	/* Put loaded blocks */
	ext4_extent_path_release(inode_ref, path);

	/* Destroy temporary data structure */
	ext4_free(path);

	return ret;
}

/**@brief Zero a range of blocks
 * @param inode_ref   I-node
 * @param fblock      starting block number to be zeroed
 * @param nblocks     number of blocks to be zeroed
 * @return Error code */
static int ext4_extent_zero_fblocks(struct ext4_inode_ref *inode_ref,
				    ext4_fsblk_t fblock,
				    ext4_lblk_t nblocks)
{
	int ret = 0;
	ext4_lblk_t i;
	u32int blocksz;

	blocksz = ext4_sb_get_block_size(&inode_ref->fs->sb);
	for (i = 0; i < nblocks; i++) {
		struct ext4_block bh = EXT4_BLOCK_ZERO();
		ret = ext4_trans_block_get_noread(inode_ref->fs->bdev, &bh,
						  fblock + i);
		if (ret != 0)
			break;

		memset(bh.data, 0, blocksz);
		ext4_trans_set_block_dirty(bh.buf);
		ret = ext4_block_set(inode_ref->fs->bdev, &bh);
		if (ret != 0)
			break;
	}
	return ret;
}

/**@brief Convert unwritten mapping to written one
 * @param inode_ref   I-node
 * @param path        Path in the extent tree
 * @param iblock      starting logical block to be converted
 * @param nblocks     number of blocks to be converted
 * @return Error code */
int ext4_extent_convert_written(struct ext4_inode_ref *inode_ref,
				struct ext4_extent_path *path,
				ext4_lblk_t iblock,
				ext4_lblk_t nblocks)
{
	int ret;
	ext4_lblk_t eiblock;
	ext4_lblk_t enblocks;
	ext4_fsblk_t efblock;
	struct ext4_extent *ext;

	ext = path[0].extent;
	assert(ext);

	eiblock = ext4_extent_get_iblock(ext);
	enblocks = ext4_extent_get_nblocks(ext);
	efblock = ext4_extent_get_fblock(ext);
	assert(EXT4_EXTENT_IN_RANGE(iblock, eiblock, enblocks));

	/* There are four cases we need to handle */
	if (iblock == eiblock && nblocks == enblocks) {
		/* Case 1: the whole extent has to be converted.
		 * This is the simplest scenario. We just need
		 * to mark the extent "written", and zero the
		 * blocks covered by the extent */
		ret = ext4_extent_zero_fblocks(inode_ref, efblock, enblocks);
		if (ret != 0)
			return ret;
		EXT4_EXT_SET_WRITTEN(ext);
		ext4_extent_path_dirty(inode_ref, path, 0);
	} else if (iblock == eiblock) {
		/* Case 2: convert the first part of the extent to written
		 * and insert an unwritten extent after that */
		ext4_lblk_t newiblock;
		ext4_lblk_t newnblocks;
		ext4_fsblk_t newfblock;
		struct ext4_extent insext;

		/* The new extent we are going to insert */
		newiblock = eiblock + nblocks;
		newnblocks = eiblock + enblocks - newiblock;
		newfblock = efblock + nblocks;

		/* Zero the blocks covered by the first part of the extent */
		ret = ext4_extent_zero_fblocks(inode_ref,
					       efblock + iblock - eiblock,
					       nblocks);
		if (ret != 0)
			return ret;

		/* Trim the current extent and convert the extent to written */
		ext4_extent_set_nblocks(ext, enblocks - nblocks, false);
		ext4_extent_path_dirty(inode_ref, path, 0);

		/* Insert the new extent */
		ext4_extent_set_iblock(&insext, newiblock);
		ext4_extent_set_nblocks(&insext, newnblocks, true);
		ext4_extent_set_fblock(&insext, newfblock);
		ret = ext4_extent_insert(inode_ref, path, &insext);
		if (ret != 0)
			/* In case when something happens during insertion
			 * we revert the trimming of the current extent */
			ext4_extent_set_nblocks(ext, nblocks, true);
	} else if (iblock + nblocks == eiblock + enblocks) {
		/* Case 3: convert the second part of the extent to written.
		 * We insert an written extent after the current extent */
		ext4_lblk_t newiblock;
		ext4_lblk_t newnblocks;
		ext4_fsblk_t newfblock;
		struct ext4_extent insext;

		/* The new extent we are going to insert */
		newiblock = iblock;
		newnblocks = nblocks;
		newfblock = efblock + iblock - eiblock;

		/* Zero the blocks covered by the first part of the extent */
		ret = ext4_extent_zero_fblocks(inode_ref, newfblock, newnblocks);
		if (ret != 0)
			return ret;

		/* Trim the current extent */
		ext4_extent_set_nblocks(ext, enblocks - nblocks, true);
		ext4_extent_path_dirty(inode_ref, path, 0);

		/* Insert the new extent */
		ext4_extent_set_iblock(&insext, newiblock);
		ext4_extent_set_nblocks(&insext, newnblocks, false);
		ext4_extent_set_fblock(&insext, newfblock);
		ret = ext4_extent_insert(inode_ref, path, &insext);
		if (ret != 0)
			/* In case when something happens during insertion
			 * we revert the trimming of the current extent */
			ext4_extent_set_nblocks(ext, nblocks, true);
	} else {
		/* Case 4: convert the middle part of the extent to written.
		 * We insert one written extent, follow by an unwritten
		 * extent */
		ext4_lblk_t newiblock[2];
		ext4_lblk_t newnblocks[2];
		ext4_fsblk_t newfblock[2];
		struct ext4_extent insext;

		/* The new extents we are going to insert */
		newiblock[0] = iblock;
		newnblocks[0] = nblocks;
		newfblock[0] = efblock + iblock - eiblock;
		newiblock[1] = iblock + nblocks;
		newnblocks[1] = eiblock + enblocks - newiblock[1];
		newfblock[1] = newfblock[0] + nblocks;

		/* Zero the blocks covered by the written extent */
		ret = ext4_extent_zero_fblocks(inode_ref, newfblock[0],
					       newnblocks[0]);
		if (ret != 0)
			return ret;

		/* We don't want to fail in the middle because we
		 * run out of space. From now on the subsequent
		 * insertions cannot fail */
		ret = ext4_extent_split(inode_ref, path, 2);
		if (ret != 0)
			return ret;

		/* Trim the current extent */
		ext4_extent_set_nblocks(ext,
					enblocks - newnblocks[0] - newnblocks[1],
					true);
		ext4_extent_path_dirty(inode_ref, path, 0);

		/* Insert the written extent first */
		ext4_extent_set_iblock(&insext, newiblock[0]);
		ext4_extent_set_nblocks(&insext, newnblocks[0], false);
		ext4_extent_set_fblock(&insext, newfblock[0]);
		ret = ext4_extent_insert(inode_ref, path, &insext);
		assert(ret == 0);

		/* Then insert the unwritten extent */
		ext4_extent_set_iblock(&insext, newiblock[1]);
		ext4_extent_set_nblocks(&insext , newnblocks[1], true);
		ext4_extent_set_fblock(&insext, newfblock[1]);
		ret = ext4_extent_insert(inode_ref, path, &insext);
		assert(ret == 0);
	}
	return ret;
}

/**@brief Check if the second extent can be appended to the first extent
 * @param ext  the first extent
 * @param ext2 the second extent
 * @return true if the two extents can be merged, otherwise false */
static bool ext4_extent_can_append(struct ext4_extent *ext,
				   struct ext4_extent *ext2)
{
	bool unwritten;
	ext4_lblk_t eiblock[2];
	ext4_lblk_t enblocks[2];
	ext4_fsblk_t efblock[2];

	eiblock[0] = ext4_extent_get_iblock(ext);
	enblocks[0] = ext4_extent_get_nblocks(ext);
	efblock[0] = ext4_extent_get_fblock(ext);
	eiblock[1] = ext4_extent_get_iblock(ext2);
	enblocks[1] = ext4_extent_get_nblocks(ext2);
	efblock[1] = ext4_extent_get_fblock(ext2);

	/* We can't merge an unwritten extent with a written
	 * extent */
	if (EXT4_EXT_IS_UNWRITTEN(ext) != EXT4_EXT_IS_UNWRITTEN(ext2))
		return false;

	unwritten = EXT4_EXT_IS_UNWRITTEN(ext);

	/* Since the starting logical block of the second
	 * extent is greater than that of the first extent,
	 * we check whether we can append the second extent
	 * to the first extent */
	if (eiblock[0] + enblocks[0] != eiblock[1] ||
	    efblock[0] + enblocks[0] != efblock[1])
		/* If the two extents are not continuous
		 * in terms of logical block range and
		 * physical block range, we return false */
		return false;

	/* Check if the total number of blocks of the two extents are
	 * too long.
	 * Note: the maximum length of unwritten extent is shorter than
	 * written extent by one block */
	if (unwritten) {
		if (enblocks[0] + enblocks[1] > EXT4_EXT_MAX_LEN_UNWRITTEN)
			return false;
	} else {
		if (enblocks[0] + enblocks[1] > EXT4_EXT_MAX_LEN_WRITTEN)
			return false;
	}

	/* The second extent can be appended to the first extent */
	return true;
}

/**@brief Check if the second extent can be prepended to the first extent
 * @param ext  the first extent
 * @param ext2 the second extent
 * @return true if the two extents can be merged, otherwise false */
static bool ext4_extent_can_prepend(struct ext4_extent *ext,
				    struct ext4_extent *ext2)
{
	bool unwritten;
	ext4_lblk_t eiblock[2];
	ext4_lblk_t enblocks[2];
	ext4_fsblk_t efblock[2];

	eiblock[0] = ext4_extent_get_iblock(ext);
	enblocks[0] = ext4_extent_get_nblocks(ext);
	efblock[0] = ext4_extent_get_fblock(ext);
	eiblock[1] = ext4_extent_get_iblock(ext2);
	enblocks[1] = ext4_extent_get_nblocks(ext2);
	efblock[1] = ext4_extent_get_fblock(ext2);

	/* We can't merge an unwritten extent with a written
	 * extent */
	if (EXT4_EXT_IS_UNWRITTEN(ext) != EXT4_EXT_IS_UNWRITTEN(ext2))
		return false;

	unwritten = EXT4_EXT_IS_UNWRITTEN(ext);

	/* Since the starting logical block of the second
	 * extent is smaller than that of the first extent,
	 * we check whether we can prepend the second extent
	 * to the first extent */
	if (eiblock[1] + enblocks[1] != eiblock[0] ||
	    efblock[1] + enblocks[1] != efblock[0])
		/* If the two extents are not continuous
		 * in terms of logical block range and
		 * physical block range, we return false */
		return false;

	/* Check if the total number of blocks of the two extents are
	 * too long.
	 * Note: the maximum length of unwritten extent is shorter than
	 * written extent by one block */
	if (unwritten) {
		if (enblocks[0] + enblocks[1] > EXT4_EXT_MAX_LEN_UNWRITTEN)
			return false;
	} else {
		if (enblocks[0] + enblocks[1] > EXT4_EXT_MAX_LEN_WRITTEN)
			return false;
	}

	/* The second extent can be prepended to the first extent */
	return true;
}

/**@brief Allocate multiple number of blocks
 * @param inode_ref I-node
 * @param goal      physical block allocation hint
 * @param nblocks   number of blocks to be allocated
 * @param fblockp   Output value - starting physical block number
 * @param nblocksp  Output value - the number of blocks allocated
 * @return Error code */
static int
ext4_extent_alloc_datablocks(struct ext4_inode_ref *inode_ref,
			     ext4_fsblk_t goal,
			     ext4_lblk_t nblocks,
			     ext4_fsblk_t *fblockp,
			     ext4_lblk_t *nblocksp)
{
	int ret = 0;
	ext4_lblk_t i;
	ext4_fsblk_t retfblock;
	ext4_lblk_t retnblocks = 0;

	for (i = 0; i < nblocks; ++i, ++retnblocks) {
		bool free = false;

		if (!i) {
			/* We allocate the first block by using
			 * ext4_balloc_alloc_block() so that we
			 * can pass allocation hint to the block
			 * allocator */
			ret = ext4_balloc_alloc_block(inode_ref,
						      goal,
						      &retfblock);
			if (ret == 0)
				free = true;
		} else {
			ext4_fsblk_t blockscnt;

			/* Do a check to make sure that we won't look into
			 * a block number larger than the total number of
			 * blocks we have on this filesystem */
			blockscnt = ext4_sb_get_blocks_cnt(&inode_ref->fs->sb);
			if (retfblock + i < blockscnt) {
				ret = ext4_balloc_try_alloc_block(inode_ref,
				    retfblock + i, &free);
			} else
				free = false;
		}

		/* Stop trying on the next block if we encounter errors, or
		 * if there is insufficient space, or if we can't allocate
		 * blocks continuously */
		if (ret != 0 || !free)
			break;
	}

	if (ret == 0) {
		*fblockp = retfblock;
		if (nblocksp)
			*nblocksp = nblocks;
	}
	return ret;
}

/**@brief Extent-based blockmap manipulation
 * @param inode_ref   I-node
 * @param iblock      starting logical block of the inode
 * @param max_nblocks maximum number of blocks to get from/allocate to blockmap
 * @param resfblockp  return physical block address of the first block of an
 * extent
 * @param create      true if caller wants to insert mapping or convert
 * unwritten mapping to written one
 * @param resnblocksp return number of blocks in an extent (must be smaller than
 * \p max_nblocks)
 * @return Error code*/
int ext4_extent_get_blocks(struct ext4_inode_ref *inode_ref,
			   ext4_lblk_t iblock,
			   ext4_lblk_t max_nblocks,
			   ext4_fsblk_t *resfblockp,
			   bool create,
			   ext4_lblk_t *resnblocksp)
{
	int ret;
	struct ext4_extent_path *path;
	struct ext4_extent *ext;
	struct ext4_extent insext;
	ext4_lblk_t eiblock;
	ext4_lblk_t enblocks;
	ext4_fsblk_t efblock;
	ext4_fsblk_t resfblock;
	ext4_lblk_t resnblocks = 0;
	ext4_fsblk_t goal;

	/* Seek to the corresponding extent */
	ret = ext4_extent_find_extent(inode_ref, iblock, &path);
	if (ret != 0)
		return ret;

	ext = path[0].extent;
	if (ext) {
		/* The extent tree is not empty */
		eiblock = ext4_extent_get_iblock(ext);
		enblocks = ext4_extent_get_nblocks(ext);
		efblock = ext4_extent_get_fblock(ext);
		if (EXT4_EXTENT_IN_RANGE(iblock, eiblock, enblocks)) {
			/* The extent exists and logical block requested falls
			 * into the range of the extent */
			resfblock = efblock + iblock - eiblock;
			resnblocks = eiblock + enblocks - iblock;

			/* Trim the result if it is larger than the maximum
			 * length the caller wants */
			if (resnblocks > max_nblocks)
				resnblocks = max_nblocks;

			if (EXT4_EXT_IS_UNWRITTEN(ext)) {
				if (create)
					/* Convert the extent to written extent
					 * if the extent is unwritten extent */
					ret = ext4_extent_convert_written(inode_ref,
									  path,
									  iblock,
									  resnblocks);
				else
					/* We are not asked to modify the blockmap
					 * so we just return a hole */
					resfblock = 0;
			}
			goto cleanup;
		}
		if (!create) {
			/* Don't waste time on finding the next extent if we
			 * are not asked to insert mapping, just return a
			 * hole */
			resfblock = 0;
			resnblocks = 1;
			goto cleanup;
		}
		if (ext4_extent_get_iblock(ext) < iblock) {
			/* Since the logical block of current extent is smaller
			 * the requested logical block, we seek to the next
			 * extent to find the maximum number of blocks we can
			 * allocate without hitting the starting logical block
			 * of the next extent */
			bool nonext;

			/* Go to the next extent */
			ret = ext4_extent_increment(inode_ref, path, &nonext);
			if (ret != 0)
				goto cleanup;

			if (!nonext) {
				/* We successfully reach the next extent */
				bool noprev;
				ext4_lblk_t neiblock;

				ext = path[0].extent;

				/* The next extent must start at greater logical
				 * block number */
				assert(ext4_extent_get_iblock(ext) >
				    iblock);

				/* Calculate the maximum number of blocks we
				 * can allocate without overlapping with the
				 * next extent */
				neiblock = ext4_extent_get_iblock(ext);
				if (max_nblocks > neiblock - iblock)
					max_nblocks = neiblock - iblock;

				/* Go back to the previous extent */
				ret = ext4_extent_decrement(inode_ref, path,
							    &noprev);
				if (ret != 0)
					goto cleanup;
				assert(!noprev);
				ext = path[0].extent;
			}
		}
	}

	/* Return a hole if we are not asked to insert mapping */
	if (!create) {
		resfblock = 0;
		resnblocks = 1;
		goto cleanup;
	}

	/* Multiple data blocks allocation */
	goal = ext4_extent_data_alloc_goal(inode_ref, path, iblock);
	ret = ext4_extent_alloc_datablocks(inode_ref, goal, max_nblocks,
					   &resfblock, &max_nblocks);
	if (ret != 0)
		goto cleanup;

	ext4_extent_set_iblock(&insext, iblock);
	ext4_extent_set_nblocks(&insext, max_nblocks, false);
	ext4_extent_set_fblock(&insext, resfblock);

	if (ext && ext4_extent_can_append(ext, &insext)) {
		/* Clang won't complain, it's just to make gcc happy */
		enblocks = ext4_extent_get_nblocks(ext);

		/* If we can append this extent to the current extent */
		ext4_extent_set_nblocks(ext, enblocks + max_nblocks,
					EXT4_EXT_IS_UNWRITTEN(ext));

		ext4_extent_path_dirty(inode_ref, path, 0);
	} else if (ext && ext4_extent_can_prepend(ext, &insext)) {
		/* Clang won't complain, it's just to make gcc happy */
		enblocks = ext4_extent_get_nblocks(ext);

		/* If we can prepend this extent to the current extent */
		ext4_extent_set_iblock(ext, iblock);
		ext4_extent_set_nblocks(ext, enblocks + max_nblocks,
					EXT4_EXT_IS_UNWRITTEN(ext));
		ext4_extent_set_fblock(ext, resfblock);

		/* If we are working on the first extent in the
		 * first leaf (in case we are actually prepending
		 * mappings) we need to update the index of nodes.
		 *
		 * NOTE: Since we don't seek to the next extent and
		 * try to modify it, prepending should not happen at
		 * any leaves except the first extent of the first leaf */
		ext4_extent_update_index(inode_ref, path, false);
		ext4_extent_path_dirty(inode_ref, path, 0);
	} else {
		/* Finally, insert a new extent into the extent tree */
		ret = ext4_extent_insert(inode_ref, path, &insext);
		if (ret != 0)
			ext4_balloc_free_blocks(inode_ref, resfblock,
						max_nblocks);
	}

	resnblocks = max_nblocks;

cleanup:
	/* Put loaded blocks */
	ext4_extent_path_release(inode_ref, path);

	/* Destroy temporary data structure */
	ext4_free(path);

	if (ret == 0) {
		if (resfblockp)
			*resfblockp = resfblock;
		if (resnblocksp)
			*resnblocksp = resnblocks;
	}

	return ret;
}
