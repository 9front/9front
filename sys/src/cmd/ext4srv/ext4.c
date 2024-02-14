#include "ext4_config.h"
#include "ext4.h"
#include "ext4_trans.h"
#include "ext4_fs.h"
#include "ext4_dir.h"
#include "ext4_inode.h"
#include "ext4_super.h"
#include "ext4_block_group.h"
#include "ext4_dir_idx.h"
#include "ext4_journal.h"

char Eexists[] = "file exists";
char Einval[] = "invalid operation";
char Eio[] = "i/o error";
char Enomem[] = "no memory";
char Enospc[] = "no space";
char Enotfound[] = "file not found";
char Eperm[] = "permission denied";
char Erdonlyfs[] = "read-only fs";

static char *
basename(char *s)
{
	char *e;
	e = utfrrune(s, '/');
	return e == nil ? s : e+1;
}

/**@brief   Mount point OS dependent lock*/
#define EXT4_MP_LOCK(_m)                                               \
	do {                                                               \
		if ((_m)->os_locks)                                            \
			(_m)->os_locks->lock((_m)->os_locks->p_user);              \
	} while (0)

/**@brief   Mount point OS dependent unlock*/
#define EXT4_MP_UNLOCK(_m)                                             \
	do {                                                               \
		if ((_m)->os_locks)                                            \
			(_m)->os_locks->unlock((_m)->os_locks->p_user);            \
	} while (0)

static bool ext4_is_dots(const u8int *name, usize name_size)
{
	if ((name_size == 1) && (name[0] == '.'))
		return true;

	if ((name_size == 2) && (name[0] == '.') && (name[1] == '.'))
		return true;

	return false;
}

static int ext4_has_children(bool *has_children, struct ext4_inode_ref *enode)
{
	struct ext4_sblock *sb = &enode->fs->sb;

	/* Check if node is directory */
	if (!ext4_inode_is_type(sb, enode->inode, EXT4_INODE_MODE_DIRECTORY)) {
		*has_children = false;
		return 0;
	}

	struct ext4_dir_iter it;
	int rc = ext4_dir_iterator_init(&it, enode, 0);
	if (rc != 0)
		return rc;

	/* Find a non-empty directory entry */
	bool found = false;
	while (it.curr != nil) {
		if (ext4_dir_en_get_inode(it.curr) != 0) {
			u16int nsize;
			nsize = ext4_dir_en_get_name_len(sb, it.curr);
			if (!ext4_is_dots(it.curr->name, nsize)) {
				found = true;
				break;
			}
		}

		rc = ext4_dir_iterator_next(&it);
		if (rc != 0) {
			ext4_dir_iterator_fini(&it);
			return rc;
		}
	}

	rc = ext4_dir_iterator_fini(&it);
	if (rc != 0)
		return rc;

	*has_children = found;

	return 0;
}

static int ext4_link(struct ext4_mountpoint *mp, struct ext4_inode_ref *parent,
		     struct ext4_inode_ref *ch, const char *n,
		     u32int len, bool rename)
{
	/* Check maximum name length */
	if (len > EXT4_DIRECTORY_FILENAME_LEN) {
		werrstr("entry name too long: %s", n);
		return -1;
	}

	/* Add entry to parent directory */
	int r = ext4_dir_add_entry(parent, n, len, ch);
	if (r != 0)
		return r;

	/* Fill new dir -> add '.' and '..' entries.
	 * Also newly allocated inode should have 0 link count.
	 */

	bool is_dir = ext4_inode_is_type(&mp->fs.sb, ch->inode,
			       EXT4_INODE_MODE_DIRECTORY);
	if (is_dir && !rename) {
		/* Initialize directory index if supported */
		if (ext4_sb_feature_com(&mp->fs.sb, EXT4_FCOM_DIR_INDEX)) {
			r = ext4_dir_dx_init(ch, parent);
			if (r != 0)
				return r;

			ext4_inode_set_flag(ch->inode, EXT4_INODE_FLAG_INDEX);
			ch->dirty = true;
		} else

		r = ext4_dir_add_entry(ch, ".", strlen("."), ch);
		if (r != 0) {
			ext4_dir_remove_entry(parent, n, strlen(n));
			return r;
		}

		r = ext4_dir_add_entry(ch, "..", strlen(".."), parent);
		if (r != 0) {
			ext4_dir_remove_entry(parent, n, strlen(n));
			ext4_dir_remove_entry(ch, ".", strlen("."));
			return r;
		}

		/*New empty directory. Two links (. and ..) */
		ext4_inode_set_links_cnt(ch->inode, 2);
		ext4_fs_inode_links_count_inc(parent);
		ch->dirty = true;
		parent->dirty = true;
		return r;
	}
	/*
	 * In case we want to rename a directory,
	 * we reset the original '..' pointer.
	 */
	if (is_dir) {
		bool idx;
		idx = ext4_inode_has_flag(ch->inode, EXT4_INODE_FLAG_INDEX);
		struct ext4_dir_search_result res;
		if (!idx) {
			r = ext4_dir_find_entry(&res, ch, "..", strlen(".."));
			if (r != 0) {
				werrstr(Eio);
				return -1;
			}

			ext4_dir_en_set_inode(res.dentry, parent->index);
			ext4_trans_set_block_dirty(res.block.buf);
			r = ext4_dir_destroy_result(ch, &res);
			if (r != 0)
				return r;

		} else {
			r = ext4_dir_dx_reset_parent_inode(ch, parent->index);
			if (r != 0)
				return r;
		}

		ext4_fs_inode_links_count_inc(parent);
		parent->dirty = true;
	}
	if (!rename) {
		ext4_fs_inode_links_count_inc(ch);
		ch->dirty = true;
	}

	return r;
}

static int ext4_unlink(struct ext4_mountpoint *mp,
		       struct ext4_inode_ref *parent,
		       struct ext4_inode_ref *child, const char *name)
{
	bool has_children;
	int rc = ext4_has_children(&has_children, child);
	if (rc != 0)
		return rc;

	/* Cannot unlink non-empty node */
	if (has_children) {
		werrstr("remove -- directory not empty");
		return -1;
	}

	/* Remove entry from parent directory */
	name = basename(name);
	rc = ext4_dir_remove_entry(parent, name, strlen(name));
	if (rc != 0)
		return rc;

	bool is_dir = ext4_inode_is_type(&mp->fs.sb, child->inode,
					 EXT4_INODE_MODE_DIRECTORY);

	/* If directory - handle links from parent */
	if (is_dir) {
		ext4_fs_inode_links_count_dec(parent);
		parent->dirty = true;
	}

	/*
	 * TODO: Update timestamps of the parent
	 * (when we have wall-clock time).
	 *
	 * ext4_inode_set_change_inode_time(parent->inode, (u32int) now);
	 * ext4_inode_set_modification_time(parent->inode, (u32int) now);
	 * parent->dirty = true;
	 */

	/*
	 * TODO: Update timestamp for inode.
	 *
	 * ext4_inode_set_change_inode_time(child->inode,
	 *     (u32int) now);
	 */
	if (ext4_inode_get_links_cnt(child->inode)) {
		ext4_fs_inode_links_count_dec(child);
		child->dirty = true;
	}

	return 0;
}

int ext4_mount(struct ext4_mountpoint *mp, struct ext4_blockdev *bd, bool read_only)
{
	int r;
	u32int bsize;
	struct ext4_bcache *bc;

	memset(mp, 0, sizeof(*mp));

	r = ext4_block_init(bd);
	if (r != 0)
		return r;

	r = ext4_fs_init(&mp->fs, bd, read_only);
	if (r != 0) {
err:
		ext4_block_fini(bd);
		return r;
	}

	bsize = ext4_sb_get_block_size(&mp->fs.sb);
	ext4_block_set_lb_size(bd, bsize);
	bc = &mp->bc;

	r = ext4_bcache_init_dynamic(bc, CONFIG_BLOCK_DEV_CACHE_SIZE, bsize);
	if (r != 0)
		goto err;

	if (bsize != bc->itemsize) {
		werrstr("unsupported block size: %d", bsize);
		ext4_bcache_fini_dynamic(bc);
		goto err;
	}

	/*Bind block cache to block device*/
	r = ext4_block_bind_bcache(bd, bc);
	if (r != 0) {
		ext4_bcache_cleanup(bc);
		ext4_bcache_fini_dynamic(bc);
		goto err;
	}

	bd->fs = &mp->fs;
	mp->mounted = true;
	return r;
}

int ext4_umount(struct ext4_mountpoint *mp)
{
	int r;

	r = ext4_fs_fini(&mp->fs);
	if (r != 0)
		goto Finish;

	mp->mounted = false;

	ext4_bcache_cleanup(mp->fs.bdev->bc);
	ext4_bcache_fini_dynamic(mp->fs.bdev->bc);

	r = ext4_block_fini(mp->fs.bdev);
Finish:
	mp->fs.bdev->fs = nil;
	return r;
}

int ext4_journal_start(struct ext4_mountpoint *mp)
{
	int r;

	if (mp->fs.read_only)
		return 0;
	if (!ext4_sb_feature_com(&mp->fs.sb, EXT4_FCOM_HAS_JOURNAL))
		return 0;

	r = jbd_get_fs(&mp->fs, &mp->jbd_fs);
	if (r != 0)
		goto Finish;

	r = jbd_journal_start(&mp->jbd_fs, &mp->jbd_journal);
	if (r != 0) {
		mp->jbd_fs.dirty = false;
		jbd_put_fs(&mp->jbd_fs);
		goto Finish;
	}
	mp->fs.jbd_fs = &mp->jbd_fs;
	mp->fs.jbd_journal = &mp->jbd_journal;

Finish:
	return r;
}

int ext4_journal_stop(struct ext4_mountpoint *mp)
{
	int r;

	if (mp->fs.read_only)
		return 0;
	if (!ext4_sb_feature_com(&mp->fs.sb, EXT4_FCOM_HAS_JOURNAL))
		return 0;
	r = jbd_journal_stop(&mp->jbd_journal);
	if (r != 0) {
		mp->jbd_fs.dirty = false;
		jbd_put_fs(&mp->jbd_fs);
		mp->fs.jbd_journal = nil;
		mp->fs.jbd_fs = nil;
		goto Finish;
	}

	r = jbd_put_fs(&mp->jbd_fs);
	if (r != 0) {
		mp->fs.jbd_journal = nil;
		mp->fs.jbd_fs = nil;
		goto Finish;
	}

	mp->fs.jbd_journal = nil;
	mp->fs.jbd_fs = nil;

Finish:
	return r;
}

int ext4_recover(struct ext4_mountpoint *mp)
{
	int r;

	EXT4_MP_LOCK(mp);
	if (!ext4_sb_feature_com(&mp->fs.sb, EXT4_FCOM_HAS_JOURNAL))
		return 0;

	struct jbd_fs *jbd_fs = ext4_calloc(1, sizeof(struct jbd_fs));
	if (!jbd_fs) {
		werrstr("memory");
		r = -1;
		goto Finish;
	}

	r = jbd_get_fs(&mp->fs, jbd_fs);
	if (r != 0) {
		ext4_free(jbd_fs);
		goto Finish;
	}

	r = jbd_recover(jbd_fs);
	jbd_put_fs(jbd_fs);
	ext4_free(jbd_fs);

	if (r == 0 && !mp->fs.read_only) {
		u32int bgid;
		u64int free_blocks_count = 0;
		u32int free_inodes_count = 0;
		struct ext4_block_group_ref bg_ref;

		/* Update superblock's stats */
		for (bgid = 0;bgid < ext4_block_group_cnt(&mp->fs.sb);bgid++) {
			r = ext4_fs_get_block_group_ref(&mp->fs, bgid, &bg_ref);
			if (r != 0)
				goto Finish;

			free_blocks_count +=
				ext4_bg_get_free_blocks_count(bg_ref.block_group,
						&mp->fs.sb);
			free_inodes_count +=
				ext4_bg_get_free_inodes_count(bg_ref.block_group,
						&mp->fs.sb);

			ext4_fs_put_block_group_ref(&bg_ref);
		}
		ext4_sb_set_free_blocks_cnt(&mp->fs.sb, free_blocks_count);
		ext4_set32(&mp->fs.sb, free_inodes_count, free_inodes_count);
		/* We don't need to save the superblock stats immediately. */
	}

Finish:
	EXT4_MP_UNLOCK(mp);
	return r;
}

int ext4_trans_start(struct ext4_mountpoint *mp)
{
	int r = 0;

	if (mp->fs.jbd_journal && !mp->fs.curr_trans) {
		struct jbd_journal *journal = mp->fs.jbd_journal;
		struct jbd_trans *trans;
		trans = jbd_journal_new_trans(journal);
		if (!trans) {
			werrstr("memory");
			r = -1;
			goto Finish;
		}
		mp->fs.curr_trans = trans;
	}
Finish:
	return r;
}

int ext4_trans_stop(struct ext4_mountpoint *mp)
{
	int r = 0;

	if (mp->fs.jbd_journal && mp->fs.curr_trans) {
		struct jbd_journal *journal = mp->fs.jbd_journal;
		struct jbd_trans *trans = mp->fs.curr_trans;
		r = jbd_journal_commit_trans(journal, trans);
		mp->fs.curr_trans = nil;
	}
	return r;
}

void ext4_trans_abort(struct ext4_mountpoint *mp)
{
	if (mp->fs.jbd_journal && mp->fs.curr_trans) {
		struct jbd_journal *journal = mp->fs.jbd_journal;
		struct jbd_trans *trans = mp->fs.curr_trans;
		jbd_journal_free_trans(journal, trans, true);
		mp->fs.curr_trans = nil;
	}
}

int ext4_mount_point_stats(struct ext4_mountpoint *mp,
			   struct ext4_mount_stats *stats)
{
	EXT4_MP_LOCK(mp);
	stats->inodes_count = ext4_get32(&mp->fs.sb, inodes_count);
	stats->free_inodes_count = ext4_get32(&mp->fs.sb, free_inodes_count);
	stats->blocks_count = ext4_sb_get_blocks_cnt(&mp->fs.sb);
	stats->free_blocks_count = ext4_sb_get_free_blocks_cnt(&mp->fs.sb);
	stats->block_size = ext4_sb_get_block_size(&mp->fs.sb);

	stats->block_group_count = ext4_block_group_cnt(&mp->fs.sb);
	stats->blocks_per_group = ext4_get32(&mp->fs.sb, blocks_per_group);
	stats->inodes_per_group = ext4_get32(&mp->fs.sb, inodes_per_group);

	memcpy(stats->volume_name, mp->fs.sb.volume_name, 16);
	EXT4_MP_UNLOCK(mp);

	return 0;
}

int ext4_mount_setup_locks(struct ext4_mountpoint *mp,
			   const struct ext4_lock *locks)
{
	mp->os_locks = locks;
	return 0;
}

/********************************FILE OPERATIONS*****************************/

static int ext4_path_check(const char *path, bool *is_goal)
{
	int i;

	for(i = 0; i < EXT4_DIRECTORY_FILENAME_LEN; ++i) {
		if (path[i] == '/') {
			*is_goal = false;
			return i;
		}

		if (path[i] == 0) {
			*is_goal = true;
			return i;
		}
	}

	return 0;
}

static bool ext4_parse_flags(const char *flags, u32int *file_flags)
{
	if (!flags)
		return false;

	if (!strcmp(flags, "r") || !strcmp(flags, "rb")) {
		*file_flags = O_RDONLY;
		return true;
	}

	if (!strcmp(flags, "w") || !strcmp(flags, "wb")) {
		*file_flags = O_WRONLY | O_CREAT | O_TRUNC;
		return true;
	}

	if (!strcmp(flags, "a") || !strcmp(flags, "ab")) {
		*file_flags = O_WRONLY | O_CREAT | O_APPEND;
		return true;
	}

	if (!strcmp(flags, "r+") || !strcmp(flags, "rb+") ||
	    !strcmp(flags, "r+b")) {
		*file_flags = O_RDWR;
		return true;
	}

	if (!strcmp(flags, "w+") || !strcmp(flags, "wb+") ||
	    !strcmp(flags, "w+b")) {
		*file_flags = O_RDWR | O_CREAT | O_TRUNC;
		return true;
	}

	if (!strcmp(flags, "a+") || !strcmp(flags, "ab+") ||
	    !strcmp(flags, "a+b")) {
		*file_flags = O_RDWR | O_CREAT | O_APPEND;
		return true;
	}

	return false;
}

static int ext4_trunc_inode(struct ext4_mountpoint *mp,
			    u32int index, u64int new_size)
{
	int r;
	struct ext4_fs *const fs = &mp->fs;
	struct ext4_inode_ref inode_ref;
	u64int inode_size;
	bool has_trans = mp->fs.jbd_journal && mp->fs.curr_trans;
	r = ext4_fs_get_inode_ref(fs, index, &inode_ref);
	if (r != 0)
		return r;

	inode_size = ext4_inode_get_size(&fs->sb, inode_ref.inode);
	ext4_fs_put_inode_ref(&inode_ref);
	if (has_trans)
		ext4_trans_stop(mp);

	while (inode_size > new_size + CONFIG_MAX_TRUNCATE_SIZE) {

		inode_size -= CONFIG_MAX_TRUNCATE_SIZE;

		ext4_trans_start(mp);
		r = ext4_fs_get_inode_ref(fs, index, &inode_ref);
		if (r != 0) {
			ext4_trans_abort(mp);
			break;
		}
		r = ext4_fs_truncate_inode(&inode_ref, inode_size);
		if (r != 0)
			ext4_fs_put_inode_ref(&inode_ref);
		else
			r = ext4_fs_put_inode_ref(&inode_ref);

		if (r != 0) {
			ext4_trans_abort(mp);
			goto Finish;
		} else
			ext4_trans_stop(mp);
	}

	if (inode_size > new_size) {

		inode_size = new_size;

		ext4_trans_start(mp);
		r = ext4_fs_get_inode_ref(fs, index, &inode_ref);
		if (r != 0) {
			ext4_trans_abort(mp);
			goto Finish;
		}
		r = ext4_fs_truncate_inode(&inode_ref, inode_size);
		if (r != 0)
			ext4_fs_put_inode_ref(&inode_ref);
		else
			r = ext4_fs_put_inode_ref(&inode_ref);

		if (r != 0)
			ext4_trans_abort(mp);
		else
			ext4_trans_stop(mp);

	}

Finish:

	if (has_trans)
		ext4_trans_start(mp);

	return r;
}

static int ext4_trunc_dir(struct ext4_mountpoint *mp,
			  struct ext4_inode_ref *parent,
			  struct ext4_inode_ref *dir)
{
	int r;
	bool is_dir = ext4_inode_is_type(&mp->fs.sb, dir->inode,
			EXT4_INODE_MODE_DIRECTORY);
	u32int block_size = ext4_sb_get_block_size(&mp->fs.sb);
	if (!is_dir) {
		werrstr("not a directory");
		return -1;
	}

	/* Initialize directory index if supported */
	if (ext4_sb_feature_com(&mp->fs.sb, EXT4_FCOM_DIR_INDEX)) {
		r = ext4_dir_dx_init(dir, parent);
		if (r != 0)
			return r;

		r = ext4_trunc_inode(mp, dir->index,
				     EXT4_DIR_DX_INIT_BCNT * block_size);
		if (r != 0)
			return r;
	} else {
		r = ext4_trunc_inode(mp, dir->index, block_size);
		if (r != 0)
			return r;
	}

	return ext4_fs_truncate_inode(dir, 0);
}

/*
 * NOTICE: if filetype is equal to EXT4_DIRENTRY_UNKNOWN,
 * any filetype of the target dir entry will be accepted.
 */
static int ext4_generic_open2(struct ext4_mountpoint *mp,
			      ext4_file *f, const char *path, int flags,
			      int ftype, u32int *parent_inode)
{
	bool is_goal = false;
	u32int imode = EXT4_INODE_MODE_DIRECTORY;
	u32int next_inode;

	int r;
	int len;
	struct ext4_dir_search_result result;
	struct ext4_inode_ref ref;

	f->mp = 0;

	struct ext4_fs *const fs = &mp->fs;
	struct ext4_sblock *const sb = &mp->fs.sb;

	if (fs->read_only && flags & O_CREAT) {
		werrstr(Erdonlyfs);
		return -1;
	}

	f->flags = flags;

	/*Load root*/
	r = ext4_fs_get_inode_ref(fs, EXT4_INODE_ROOT_INDEX, &ref);
	if (r != 0)
		return r;

	if (parent_inode)
		*parent_inode = ref.index;

	while (1) {

		len = ext4_path_check(path, &is_goal);
		if (!len) {
			/*If root open was request.*/
			if (ftype == EXT4_DE_DIR || ftype == EXT4_DE_UNKNOWN)
				if (is_goal)
					break;

Notfound:
			werrstr(Enotfound);
			r = -1;
			break;
		}

		r = ext4_dir_find_entry(&result, &ref, path, len);
		if (r != 0) {

			/*Destroy last result*/
			ext4_dir_destroy_result(&ref, &result);
			if (r != EXT4_ERR_NOT_FOUND)
				break;

			if (!(f->flags & O_CREAT))
				break;

			/*O_CREAT allows create new entry*/
			struct ext4_inode_ref child_ref;
			r = ext4_fs_alloc_inode(fs, &child_ref,
					is_goal ? ftype : EXT4_DE_DIR);

			if (r != 0)
				break;

			ext4_fs_inode_blocks_init(fs, &child_ref);

			/*Link with root dir.*/
			r = ext4_link(mp, &ref, &child_ref, path, len, false);
			if (r != 0) {
				/*Fail. Free new inode.*/
				ext4_fs_free_inode(&child_ref);
				/*We do not want to write new inode.
				  But block has to be released.*/
				child_ref.dirty = false;
				ext4_fs_put_inode_ref(&child_ref);
				break;
			}

			ext4_fs_put_inode_ref(&child_ref);
			continue;
		}

		if (parent_inode)
			*parent_inode = ref.index;

		next_inode = ext4_dir_en_get_inode(result.dentry);
		if (ext4_sb_feature_incom(sb, EXT4_FINCOM_FILETYPE)) {
			u8int t;
			t = ext4_dir_en_get_inode_type(sb, result.dentry);
			imode = ext4_fs_correspond_inode_mode(t);
		} else {
			struct ext4_inode_ref child_ref;
			r = ext4_fs_get_inode_ref(fs, next_inode, &child_ref);
			if (r != 0)
				break;

			imode = ext4_inode_type(sb, child_ref.inode);
			ext4_fs_put_inode_ref(&child_ref);
		}

		r = ext4_dir_destroy_result(&ref, &result);
		if (r != 0)
			break;

		/*If expected file error*/
		if (imode != EXT4_INODE_MODE_DIRECTORY && !is_goal)
			goto Notfound;

		if (ftype != EXT4_DE_UNKNOWN) {
			bool df = imode != ext4_fs_correspond_inode_mode(ftype);
			if (df && is_goal)
				goto Notfound;
		}

		r = ext4_fs_put_inode_ref(&ref);
		if (r != 0)
			break;

		r = ext4_fs_get_inode_ref(fs, next_inode, &ref);
		if (r != 0)
			break;

		if (is_goal)
			break;

		path += len + 1;
	}

	if (r != 0) {
		ext4_fs_put_inode_ref(&ref);
		return r;
	}

	if (is_goal) {
		if ((f->flags & O_TRUNC) && (imode == EXT4_INODE_MODE_FILE)) {
			r = ext4_trunc_inode(mp, ref.index, 0);
			if (r != 0) {
				ext4_fs_put_inode_ref(&ref);
				return r;
			}
		}

		f->mp = mp;
		f->fsize = ext4_inode_get_size(sb, ref.inode);
		f->inode = ref.index;
		f->fpos = 0;

		if (f->flags & O_APPEND)
			f->fpos = f->fsize;
	}

	return ext4_fs_put_inode_ref(&ref);
}

/****************************************************************************/

static int ext4_generic_open(struct ext4_mountpoint *mp,
			     ext4_file *f, const char *path, const char *flags,
			     bool file_expect, u32int *parent_inode)
{
	u32int iflags;
	int filetype;
	int r;

	if (ext4_parse_flags(flags, &iflags) == false)
		return -1;

	if (file_expect == true)
		filetype = EXT4_DE_REG_FILE;
	else
		filetype = EXT4_DE_DIR;

	if (iflags & O_CREAT)
		ext4_trans_start(mp);

	r = ext4_generic_open2(mp, f, path, iflags, filetype, parent_inode);

	if (iflags & O_CREAT) {
		if (r == 0)
			ext4_trans_stop(mp);
		else
			ext4_trans_abort(mp);
	}

	return r;
}

static int ext4_create_hardlink(struct ext4_mountpoint *mp,
		const char *path,
		struct ext4_inode_ref *child_ref, bool rename)
{
	bool is_goal = false;
	u32int inode_mode;
	u32int next_inode;

	int r;
	int len;
	struct ext4_dir_search_result result;
	struct ext4_inode_ref ref;

	struct ext4_fs *const fs = &mp->fs;
	struct ext4_sblock *const sb = &mp->fs.sb;

	/*Load root*/
	r = ext4_fs_get_inode_ref(fs, EXT4_INODE_ROOT_INDEX, &ref);
	if (r != 0)
		return r;

	while (1) {

		len = ext4_path_check(path, &is_goal);
		if (!len) {
			/*If root open was request.*/
			werrstr(Enotfound);
			r = -1;
			break;
		}

		r = ext4_dir_find_entry(&result, &ref, path, len);
		if (r != 0) {
			/*Destroy last result*/
			ext4_dir_destroy_result(&ref, &result);

			if (r != EXT4_ERR_NOT_FOUND || !is_goal)
				break;

			/*Link with root dir.*/
			r = ext4_link(mp, &ref, child_ref, path, len, rename);
			break;
		} else if (r == 0 && is_goal) {
			/*Destroy last result*/
			ext4_dir_destroy_result(&ref, &result);
			werrstr(Eexists);
			r = -1;
			break;
		}

		next_inode = result.dentry->inode;
		if (ext4_sb_feature_incom(sb, EXT4_FINCOM_FILETYPE)) {
			u8int t;
			t = ext4_dir_en_get_inode_type(sb, result.dentry);
			inode_mode = ext4_fs_correspond_inode_mode(t);
		} else {
			struct ext4_inode_ref child_ref;
			r = ext4_fs_get_inode_ref(fs, next_inode, &child_ref);
			if (r != 0)
				break;

			inode_mode = ext4_inode_type(sb, child_ref.inode);
			ext4_fs_put_inode_ref(&child_ref);
		}

		r = ext4_dir_destroy_result(&ref, &result);
		if (r != 0)
			break;

		if (inode_mode != EXT4_INODE_MODE_DIRECTORY) {
			werrstr(is_goal ? Eexists : Enotfound);
			r = -1;
			break;
		}

		r = ext4_fs_put_inode_ref(&ref);
		if (r != 0)
			break;

		r = ext4_fs_get_inode_ref(fs, next_inode, &ref);
		if (r != 0)
			break;

		if (is_goal)
			break;

		path += len + 1;
	};

	if (r != 0) {
		ext4_fs_put_inode_ref(&ref);
		return r;
	}

	r = ext4_fs_put_inode_ref(&ref);
	return r;
}

static int ext4_remove_orig_reference(struct ext4_mountpoint *mp,
				      const char *path,
				      struct ext4_inode_ref *parent_ref,
				      struct ext4_inode_ref *child_ref)
{
	int r;

	/* Remove entry from parent directory */
	r = ext4_dir_remove_entry(parent_ref, path, strlen(path));
	if (r != 0)
		goto Finish;

	if (ext4_inode_is_type(&mp->fs.sb, child_ref->inode,
			       EXT4_INODE_MODE_DIRECTORY)) {
		ext4_fs_inode_links_count_dec(parent_ref);
		parent_ref->dirty = true;
	}
Finish:
	return r;
}

int ext4_flink(struct ext4_mountpoint *mp, const char *path, const char *hardlink_path)
{
	int r;
	ext4_file f;
	bool child_loaded = false;
	u32int parent_inode, child_inode;
	struct ext4_inode_ref child_ref;

	if (mp->fs.read_only) {
		werrstr(Erdonlyfs);
		return -1;
	}

	EXT4_MP_LOCK(mp);
	r = ext4_generic_open2(mp, &f, path, O_RDONLY, EXT4_DE_UNKNOWN, &parent_inode);
	if (r != 0) {
		EXT4_MP_UNLOCK(mp);
		return r;
	}

	child_inode = f.inode;
	ext4_fclose(&f);
	ext4_trans_start(mp);

	/*We have file to unlink. Load it.*/
	r = ext4_fs_get_inode_ref(&mp->fs, child_inode, &child_ref);
	if (r != 0)
		goto Finish;

	child_loaded = true;

	/* Creating hardlink for directory is not allowed. */
	if (ext4_inode_is_type(&mp->fs.sb, child_ref.inode, EXT4_INODE_MODE_DIRECTORY)) {
		werrstr("is a directory");
		r = -1;
		goto Finish;
	}

	r = ext4_create_hardlink(mp, hardlink_path, &child_ref, false);

Finish:
	if (child_loaded)
		ext4_fs_put_inode_ref(&child_ref);

	if (r != 0)
		ext4_trans_abort(mp);
	else
		ext4_trans_stop(mp);

	EXT4_MP_UNLOCK(mp);
	return r;

}

int ext4_frename(struct ext4_mountpoint *mp, const char *path, const char *new_path)
{
	int r;
	ext4_file f;
	bool parent_loaded = false, child_loaded = false;
	u32int parent_inode, child_inode;
	struct ext4_inode_ref child_ref, parent_ref;

	if (mp->fs.read_only) {
		werrstr(Erdonlyfs);
		return -1;
	}

	EXT4_MP_LOCK(mp);

	r = ext4_generic_open2(mp, &f, path, O_RDONLY, EXT4_DE_UNKNOWN, &parent_inode);
	if (r != 0) {
		EXT4_MP_UNLOCK(mp);
		return r;
	}

	child_inode = f.inode;
	ext4_fclose(&f);
	ext4_trans_start(mp);

	/*Load parent*/
	r = ext4_fs_get_inode_ref(&mp->fs, parent_inode, &parent_ref);
	if (r != 0)
		goto Finish;

	parent_loaded = true;

	/*We have file to unlink. Load it.*/
	r = ext4_fs_get_inode_ref(&mp->fs, child_inode, &child_ref);
	if (r != 0)
		goto Finish;

	child_loaded = true;

	r = ext4_create_hardlink(mp, new_path, &child_ref, true);
	if (r != 0)
		goto Finish;

	r = ext4_remove_orig_reference(mp, basename(path), &parent_ref, &child_ref);
	if (r != 0)
		goto Finish;

Finish:
	if (parent_loaded)
		ext4_fs_put_inode_ref(&parent_ref);

	if (child_loaded)
		ext4_fs_put_inode_ref(&child_ref);

	if (r != 0)
		ext4_trans_abort(mp);
	else
		ext4_trans_stop(mp);

	EXT4_MP_UNLOCK(mp);
	return r;

}

/****************************************************************************/

int ext4_get_sblock(struct ext4_mountpoint *mp, struct ext4_sblock **sb)
{
	*sb = &mp->fs.sb;
	return 0;
}

int ext4_cache_write_back(struct ext4_mountpoint *mp, bool on)
{
	int ret;

	EXT4_MP_LOCK(mp);
	ret = ext4_block_cache_write_back(mp->fs.bdev, on);
	EXT4_MP_UNLOCK(mp);
	return ret;
}

int ext4_cache_flush(struct ext4_mountpoint *mp)
{
	int ret;

	EXT4_MP_LOCK(mp);
	ret = ext4_block_cache_flush(mp->fs.bdev);
	EXT4_MP_UNLOCK(mp);
	return ret;
}

int ext4_fremove(struct ext4_mountpoint *mp, const char *path)
{
	ext4_file f;
	u32int parent_inode;
	u32int child_inode;
	int r;
	struct ext4_inode_ref child;
	struct ext4_inode_ref parent;

	if (mp->fs.read_only) {
		werrstr(Erdonlyfs);
		return -1;
	}

	EXT4_MP_LOCK(mp);
	r = ext4_generic_open2(mp, &f, path, O_RDONLY, EXT4_DE_UNKNOWN, &parent_inode);
	if (r != 0) {
		EXT4_MP_UNLOCK(mp);
		return r;
	}

	child_inode = f.inode;
	ext4_fclose(&f);
	ext4_trans_start(mp);

	/*Load parent*/
	r = ext4_fs_get_inode_ref(&mp->fs, parent_inode, &parent);
	if (r != 0) {
		ext4_trans_abort(mp);
		EXT4_MP_UNLOCK(mp);
		return r;
	}

	/*We have file to delete. Load it.*/
	r = ext4_fs_get_inode_ref(&mp->fs, child_inode, &child);
	if (r != 0) {
		ext4_fs_put_inode_ref(&parent);
		ext4_trans_abort(mp);
		EXT4_MP_UNLOCK(mp);
		return r;
	}
	/* We do not allow opening files here. */
	if (ext4_inode_type(&mp->fs.sb, child.inode) ==
	    EXT4_INODE_MODE_DIRECTORY) {
		ext4_fs_put_inode_ref(&parent);
		ext4_fs_put_inode_ref(&child);
		ext4_trans_abort(mp);
		EXT4_MP_UNLOCK(mp);
		return r;
	}

	/*Link count will be zero, the inode should be freed. */
	if (ext4_inode_get_links_cnt(child.inode) == 1) {
		ext4_block_cache_write_back(mp->fs.bdev, 1);
		r = ext4_trunc_inode(mp, child.index, 0);
		if (r != 0) {
			ext4_fs_put_inode_ref(&parent);
			ext4_fs_put_inode_ref(&child);
			ext4_trans_abort(mp);
			EXT4_MP_UNLOCK(mp);
			return r;
		}
		ext4_block_cache_write_back(mp->fs.bdev, 0);
	}

	/*Unlink from parent*/
	r = ext4_unlink(mp, &parent, &child, path);
	if (r != 0)
		goto Finish;

	/*Link count is zero, the inode should be freed. */
	if (!ext4_inode_get_links_cnt(child.inode)) {
		ext4_inode_set_del_time(child.inode, -1L);

		r = ext4_fs_free_inode(&child);
		if (r != 0)
			goto Finish;
	}

Finish:
	ext4_fs_put_inode_ref(&child);
	ext4_fs_put_inode_ref(&parent);

	if (r != 0)
		ext4_trans_abort(mp);
	else
		ext4_trans_stop(mp);

	EXT4_MP_UNLOCK(mp);
	return r;
}

int ext4_fopen(struct ext4_mountpoint *mp, ext4_file *file, const char *path, const char *flags)
{
	int r;

	EXT4_MP_LOCK(mp);

	ext4_block_cache_write_back(mp->fs.bdev, 1);
	r = ext4_generic_open(mp, file, path, flags, true, nil);
	ext4_block_cache_write_back(mp->fs.bdev, 0);

	EXT4_MP_UNLOCK(mp);
	return r;
}

int ext4_fopen2(struct ext4_mountpoint *mp, ext4_file *file, const char *path, int flags)
{
	int r;
	int filetype;

	filetype = EXT4_DE_REG_FILE;

	EXT4_MP_LOCK(mp);
	ext4_block_cache_write_back(mp->fs.bdev, 1);

	if (flags & O_CREAT)
		ext4_trans_start(mp);

	r = ext4_generic_open2(mp, file, path, flags, filetype, nil);

	if (flags & O_CREAT) {
		if (r == 0)
			ext4_trans_stop(mp);
		else
			ext4_trans_abort(mp);
	}

	ext4_block_cache_write_back(mp->fs.bdev, 0);
	EXT4_MP_UNLOCK(mp);

	return r;
}

int ext4_fclose(ext4_file *file)
{
	assert(file && file->mp);

	file->mp = 0;
	file->flags = 0;
	file->inode = 0;
	file->fpos = 0;
	file->fsize = 0;

	return 0;
}

static int ext4_ftruncate_no_lock(ext4_file *file, u64int size)
{
	struct ext4_inode_ref ref;
	int r;


	r = ext4_fs_get_inode_ref(&file->mp->fs, file->inode, &ref);
	if (r != 0) {
		EXT4_MP_UNLOCK(file->mp);
		return r;
	}

	/*Sync file size*/
	file->fsize = ext4_inode_get_size(&file->mp->fs.sb, ref.inode);
	if (file->fsize <= size) {
		werrstr("space preallocation not supported");
		r = -1;
		goto Finish;
	}

	/*Start write back cache mode.*/
	r = ext4_block_cache_write_back(file->mp->fs.bdev, 1);
	if (r != 0)
		goto Finish;

	r = ext4_trunc_inode(file->mp, ref.index, size);
	if (r != 0)
		goto Finish;

	file->fsize = size;
	if (file->fpos > size)
		file->fpos = size;

	/*Stop write back cache mode*/
	ext4_block_cache_write_back(file->mp->fs.bdev, 0);

	if (r != 0)
		goto Finish;

Finish:
	ext4_fs_put_inode_ref(&ref);
	return r;

}

int ext4_ftruncate(ext4_file *f, u64int size)
{
	int r;
	assert(f && f->mp);

	if (f->mp->fs.read_only) {
		werrstr(Erdonlyfs);
		return -1;
	}

	if ((f->flags & O_WRMASK) == 0) {
		werrstr(Eperm);
		return -1;
	}

	EXT4_MP_LOCK(f->mp);

	ext4_trans_start(f->mp);

	r = ext4_ftruncate_no_lock(f, size);

	if( r == 0 )
		ext4_trans_stop(f->mp);
	else
		ext4_trans_abort(f->mp);

	EXT4_MP_UNLOCK(f->mp);
	return r;
}

int ext4_fread(ext4_file *file, void *buf, usize size, usize *rcnt)
{
	u32int unalg;
	u32int iblock_idx;
	u32int iblock_last;
	u32int block_size;

	ext4_fsblk_t fblock;
	ext4_fsblk_t fblock_start;
	u32int fblock_count;

	u8int *u8_buf = buf;
	int r;
	struct ext4_inode_ref ref;

	assert(file && file->mp);

	if (file->flags & O_WRONLY) {
		werrstr(Eperm);
		return -1;
	}

	if (!size)
		return 0;

	EXT4_MP_LOCK(file->mp);

	struct ext4_fs *const fs = &file->mp->fs;
	struct ext4_sblock *const sb = &file->mp->fs.sb;

	if (rcnt)
		*rcnt = 0;

	r = ext4_fs_get_inode_ref(fs, file->inode, &ref);
	if (r != 0) {
		EXT4_MP_UNLOCK(file->mp);
		return r;
	}

	/*Sync file size*/
	file->fsize = ext4_inode_get_size(sb, ref.inode);

	block_size = ext4_sb_get_block_size(sb);
	size = ((u64int)size > (file->fsize - file->fpos))
		? ((usize)(file->fsize - file->fpos)) : size;

	iblock_idx = (u32int)((file->fpos) / block_size);
	iblock_last = (u32int)((file->fpos + size) / block_size);
	unalg = (file->fpos) % block_size;

	/*If the size of symlink is smaller than 60 bytes*/
	bool softlink;
	softlink = ext4_inode_is_type(sb, ref.inode, EXT4_INODE_MODE_SOFTLINK);
	if (softlink && file->fsize < sizeof(ref.inode->blocks)
		     && !ext4_inode_get_blocks_count(sb, ref.inode)) {

		char *content = (char *)ref.inode->blocks;
		if (file->fpos < file->fsize) {
			usize len = size;
			if (unalg + size > (u32int)file->fsize)
				len = (u32int)file->fsize - unalg;
			memcpy(buf, content + unalg, len);
			if (rcnt)
				*rcnt = len;

		}

		r = 0;
		goto Finish;
	}

	if (unalg) {
		usize len =  size;
		if (size > (block_size - unalg))
			len = block_size - unalg;

		r = ext4_fs_get_inode_dblk_idx(&ref, iblock_idx, &fblock, true);
		if (r != 0)
			goto Finish;

		/* Do we get an unwritten range? */
		if (fblock != 0) {
			u64int off = fblock * block_size + unalg;
			r = ext4_block_readbytes(file->mp->fs.bdev, off, u8_buf, len);
			if (r != 0)
				goto Finish;

		} else {
			/* Yes, we do. */
			memset(u8_buf, 0, len);
		}

		u8_buf += len;
		size -= len;
		file->fpos += len;

		if (rcnt)
			*rcnt += len;

		iblock_idx++;
	}

	fblock_start = 0;
	fblock_count = 0;
	while (size >= block_size) {
		while (iblock_idx < iblock_last) {
			r = ext4_fs_get_inode_dblk_idx(&ref, iblock_idx,
						       &fblock, true);
			if (r != 0)
				goto Finish;

			iblock_idx++;

			if (!fblock_start)
				fblock_start = fblock;

			if ((fblock_start + fblock_count) != fblock)
				break;

			fblock_count++;
		}

		r = ext4_blocks_get_direct(file->mp->fs.bdev, u8_buf, fblock_start,
					   fblock_count);
		if (r != 0)
			goto Finish;

		size -= block_size * fblock_count;
		u8_buf += block_size * fblock_count;
		file->fpos += block_size * fblock_count;

		if (rcnt)
			*rcnt += block_size * fblock_count;

		fblock_start = fblock;
		fblock_count = 1;
	}

	if (size) {
		u64int off;
		r = ext4_fs_get_inode_dblk_idx(&ref, iblock_idx, &fblock, true);
		if (r != 0)
			goto Finish;

		off = fblock * block_size;
		r = ext4_block_readbytes(file->mp->fs.bdev, off, u8_buf, size);
		if (r != 0)
			goto Finish;

		file->fpos += size;

		if (rcnt)
			*rcnt += size;
	}

Finish:
	ext4_fs_put_inode_ref(&ref);
	EXT4_MP_UNLOCK(file->mp);
	return r;
}

int ext4_fwrite(ext4_file *file, const void *buf, usize size, usize *wcnt)
{
	u32int unalg;
	u32int iblk_idx;
	u32int iblock_last;
	u32int ifile_blocks;
	u32int block_size;

	u32int fblock_count;
	ext4_fsblk_t fblk;
	ext4_fsblk_t fblock_start;

	struct ext4_inode_ref ref;
	const u8int *u8_buf = buf;
	int r, rr = 0;

	assert(file && file->mp);

	if(wcnt)
		*wcnt = 0;

	if (file->mp->fs.read_only) {
		werrstr(Erdonlyfs);
		return -1;
	}

	if ((file->flags & O_WRMASK) == 0) {
		werrstr(Eperm);
		return -1;
	}

	if (!size)
		return 0;

	EXT4_MP_LOCK(file->mp);
	ext4_trans_start(file->mp);

	struct ext4_fs *const fs = &file->mp->fs;
	struct ext4_sblock *const sb = &file->mp->fs.sb;

	r = ext4_fs_get_inode_ref(fs, file->inode, &ref);
	if (r != 0) {
		ext4_trans_abort(file->mp);
		EXT4_MP_UNLOCK(file->mp);
		return r;
	}

	/*Sync file size*/
	file->fsize = ext4_inode_get_size(sb, ref.inode);
	block_size = ext4_sb_get_block_size(sb);

	iblock_last = (file->fpos + size) / block_size;
	iblk_idx = file->fpos / block_size;
	ifile_blocks = (file->fsize + block_size - 1) / block_size;
	unalg = file->fpos % block_size;

	if (unalg) {
		usize len =  size;
		u64int off;
		if (size > (block_size - unalg))
			len = block_size - unalg;

		r = ext4_fs_init_inode_dblk_idx(&ref, iblk_idx, &fblk);
		if (r != 0)
			goto Finish;

		off = fblk * block_size + unalg;
		r = ext4_block_writebytes(file->mp->fs.bdev, off, u8_buf, len);
		if (r != 0)
			goto Finish;

		u8_buf += len;
		size -= len;
		file->fpos += len;

		if (wcnt)
			*wcnt += len;

		iblk_idx++;
	}

	/*Start write back cache mode.*/
	r = ext4_block_cache_write_back(file->mp->fs.bdev, 1);
	if (r != 0)
		goto Finish;

	fblock_start = 0;
	fblock_count = 0;
	while (size >= block_size) {

		while (iblk_idx < iblock_last) {
			if (iblk_idx < ifile_blocks) {
				r = ext4_fs_init_inode_dblk_idx(&ref, iblk_idx,
								&fblk);
				if (r != 0)
					goto Finish;
			} else {
				rr = ext4_fs_append_inode_dblk(&ref, &fblk,
							       &iblk_idx);
				if (rr != 0) {
					/* Unable to append more blocks. But
					 * some block might be allocated already
					 * */
					break;
				}
			}

			iblk_idx++;

			if (!fblock_start) {
				fblock_start = fblk;
			}

			if ((fblock_start + fblock_count) != fblk)
				break;

			fblock_count++;
		}

		r = ext4_blocks_set_direct(file->mp->fs.bdev, u8_buf, fblock_start,
					   fblock_count);
		if (r != 0)
			break;

		size -= block_size * fblock_count;
		u8_buf += block_size * fblock_count;
		file->fpos += block_size * fblock_count;

		if (wcnt)
			*wcnt += block_size * fblock_count;

		fblock_start = fblk;
		fblock_count = 1;

		if (rr != 0) {
			/*ext4_fs_append_inode_block has failed and no
			 * more blocks might be written. But node size
			 * should be updated.*/
			/* FIXME wth is happening here exactly? */
			//r = rr;
			goto out_fsize;
		}
	}

	/*Stop write back cache mode*/
	ext4_block_cache_write_back(file->mp->fs.bdev, 0);

	if (r != 0)
		goto Finish;

	if (size) {
		u64int off;
		if (iblk_idx < ifile_blocks) {
			r = ext4_fs_init_inode_dblk_idx(&ref, iblk_idx, &fblk);
			if (r != 0)
				goto Finish;
		} else {
			r = ext4_fs_append_inode_dblk(&ref, &fblk, &iblk_idx);
			if (r != 0)
				/*Node size sholud be updated.*/
				goto out_fsize;
		}

		off = fblk * block_size;
		r = ext4_block_writebytes(file->mp->fs.bdev, off, u8_buf, size);
		if (r != 0)
			goto Finish;

		file->fpos += size;

		if (wcnt)
			*wcnt += size;
	}

out_fsize:
	if (file->fpos > file->fsize) {
		file->fsize = file->fpos;
		ext4_inode_set_size(ref.inode, file->fsize);
		ref.dirty = true;
	}

Finish:
	r = ext4_fs_put_inode_ref(&ref);

	if (r != 0)
		ext4_trans_abort(file->mp);
	else
		ext4_trans_stop(file->mp);

	EXT4_MP_UNLOCK(file->mp);
	return r;
}

int ext4_fseek(ext4_file *file, s64int offset, int origin)
{
	switch (origin) {
	case 2:
		offset += file->fsize;
	if(0){
	case 1:
		offset += file->fpos;
		/* slippery slope */
	}
	case 0:
		if(offset < 0)
			offset = 0;
		else if(offset > file->fsize)
			offset = file->fsize;

		file->fpos = offset;
		break;
	}

	return 0;
}

u64int ext4_ftell(ext4_file *file)
{
	return file->fpos;
}

u64int ext4_fsize(ext4_file *file)
{
	return file->fsize;
}


static int ext4_trans_get_inode_ref(struct ext4_mountpoint *mp,
				    const char *path,
				    struct ext4_inode_ref *inode_ref)
{
	int r;
	ext4_file f;

	r = ext4_generic_open2(mp, &f, path, O_RDONLY, EXT4_DE_UNKNOWN, nil);
	if (r != 0)
		return r;

	ext4_trans_start(mp);

	r = ext4_fs_get_inode_ref(&mp->fs, f.inode, inode_ref);
	if (r != 0) {
		ext4_trans_abort(mp);
		return r;
	}

	return r;
}

static int ext4_trans_put_inode_ref(struct ext4_mountpoint *mp,
				    struct ext4_inode_ref *inode_ref)
{
	int r;

	r = ext4_fs_put_inode_ref(inode_ref);
	if (r != 0)
		ext4_trans_abort(mp);
	else
		ext4_trans_stop(mp);

	return r;
}


int ext4_raw_inode_fill(struct ext4_mountpoint *mp,
			const char *path, u32int *ret_ino,
			struct ext4_inode *inode)
{
	int r;
	ext4_file f;
	struct ext4_inode_ref inode_ref;

	EXT4_MP_LOCK(mp);

	r = ext4_generic_open2(mp, &f, path, O_RDONLY, EXT4_DE_UNKNOWN, nil);
	if (r != 0) {
		EXT4_MP_UNLOCK(mp);
		return r;
	}

	/*Load parent*/
	r = ext4_fs_get_inode_ref(&mp->fs, f.inode, &inode_ref);
	if (r != 0) {
		EXT4_MP_UNLOCK(mp);
		return r;
	}

	if (ret_ino)
		*ret_ino = f.inode;
	if (inode)
		memcpy(inode, inode_ref.inode, sizeof(struct ext4_inode));

	ext4_fs_put_inode_ref(&inode_ref);
	EXT4_MP_UNLOCK(mp);

	return r;
}

int ext4_inode_exist(struct ext4_mountpoint *mp, const char *path, int type)
{
	int r;
	ext4_file f;

	EXT4_MP_LOCK(mp);
	r = ext4_generic_open2(mp, &f, path, O_RDONLY, type, nil);
	EXT4_MP_UNLOCK(mp);

	return r;
}

int ext4_mode_set(struct ext4_mountpoint *mp, const char *path, u32int mode)
{
	int r;
	u32int orig_mode;
	struct ext4_inode_ref inode_ref;

	if (mp->fs.read_only) {
		werrstr(Erdonlyfs);
		return -1;
	}

	EXT4_MP_LOCK(mp);

	r = ext4_trans_get_inode_ref(mp, path, &inode_ref);
	if (r != 0)
		goto Finish;

	orig_mode = ext4_inode_get_mode(&mp->fs.sb, inode_ref.inode);
	orig_mode &= ~0xFFF;
	orig_mode |= mode & 0xFFF;
	ext4_inode_set_mode(&mp->fs.sb, inode_ref.inode, orig_mode);

	inode_ref.dirty = true;
	r = ext4_trans_put_inode_ref(mp, &inode_ref);

	Finish:
	EXT4_MP_UNLOCK(mp);

	return r;
}

int ext4_owner_set(struct ext4_mountpoint *mp, const char *path, u32int uid, u32int gid)
{
	int r;
	struct ext4_inode_ref inode_ref;

	if (mp->fs.read_only) {
		werrstr(Erdonlyfs);
		return -1;
	}

	EXT4_MP_LOCK(mp);

	r = ext4_trans_get_inode_ref(mp, path, &inode_ref);
	if (r != 0)
		goto Finish;

	ext4_inode_set_uid(inode_ref.inode, uid);
	ext4_inode_set_gid(inode_ref.inode, gid);

	inode_ref.dirty = true;
	r = ext4_trans_put_inode_ref(mp, &inode_ref);

	Finish:
	EXT4_MP_UNLOCK(mp);

	return r;
}

int ext4_mode_get(struct ext4_mountpoint *mp, const char *path, u32int *mode)
{
	struct ext4_inode_ref inode_ref;
	ext4_file f;
	int r;

	EXT4_MP_LOCK(mp);

	r = ext4_generic_open2(mp, &f, path, O_RDONLY, EXT4_DE_UNKNOWN, nil);
	if (r != 0)
		goto Finish;

	r = ext4_fs_get_inode_ref(&mp->fs, f.inode, &inode_ref);
	if (r != 0)
		goto Finish;

	*mode = ext4_inode_get_mode(&mp->fs.sb, inode_ref.inode);
	r = ext4_fs_put_inode_ref(&inode_ref);

	Finish:
	EXT4_MP_UNLOCK(mp);

	return r;
}

int ext4_owner_get(struct ext4_mountpoint *mp, const char *path, u32int *uid, u32int *gid)
{
	struct ext4_inode_ref inode_ref;
	ext4_file f;
	int r;
	EXT4_MP_LOCK(mp);

	r = ext4_generic_open2(mp, &f, path, O_RDONLY, EXT4_DE_UNKNOWN, nil);
	if (r != 0)
		goto Finish;

	r = ext4_fs_get_inode_ref(&mp->fs, f.inode, &inode_ref);
	if (r != 0)
		goto Finish;

	*uid = ext4_inode_get_uid(inode_ref.inode);
	*gid = ext4_inode_get_gid(inode_ref.inode);
	r = ext4_fs_put_inode_ref(&inode_ref);

	Finish:
	EXT4_MP_UNLOCK(mp);

	return r;
}

int ext4_atime_set(struct ext4_mountpoint *mp, const char *path, u32int atime)
{
	struct ext4_inode_ref inode_ref;
	int r;

	if (mp->fs.read_only) {
		werrstr(Erdonlyfs);
		return -1;
	}

	EXT4_MP_LOCK(mp);

	r = ext4_trans_get_inode_ref(mp, path, &inode_ref);
	if (r != 0)
		goto Finish;

	ext4_inode_set_access_time(inode_ref.inode, atime);
	inode_ref.dirty = true;
	r = ext4_trans_put_inode_ref(mp, &inode_ref);

	Finish:
	EXT4_MP_UNLOCK(mp);

	return r;
}

int ext4_mtime_set(struct ext4_mountpoint *mp, const char *path, u32int mtime)
{
	struct ext4_inode_ref inode_ref;
	int r;

	if (mp->fs.read_only) {
		werrstr(Erdonlyfs);
		return -1;
	}

	EXT4_MP_LOCK(mp);

	r = ext4_trans_get_inode_ref(mp, path, &inode_ref);
	if (r != 0)
		goto Finish;

	ext4_inode_set_modif_time(inode_ref.inode, mtime);
	inode_ref.dirty = true;
	r = ext4_trans_put_inode_ref(mp, &inode_ref);

	Finish:
	EXT4_MP_UNLOCK(mp);

	return r;
}

int ext4_ctime_set(struct ext4_mountpoint *mp, const char *path, u32int ctime)
{
	struct ext4_inode_ref inode_ref;
	int r;

	if (mp->fs.read_only) {
		werrstr(Erdonlyfs);
		return -1;
	}

	EXT4_MP_LOCK(mp);

	r = ext4_trans_get_inode_ref(mp, path, &inode_ref);
	if (r != 0)
		goto Finish;

	ext4_inode_set_change_inode_time(inode_ref.inode, ctime);
	inode_ref.dirty = true;
	r = ext4_trans_put_inode_ref(mp, &inode_ref);

	Finish:
	EXT4_MP_UNLOCK(mp);

	return r;
}

int ext4_atime_get(struct ext4_mountpoint *mp, const char *path, u32int *atime)
{
	struct ext4_inode_ref inode_ref;
	ext4_file f;
	int r;

	EXT4_MP_LOCK(mp);

	r = ext4_generic_open2(mp, &f, path, O_RDONLY, EXT4_DE_UNKNOWN, nil);
	if (r != 0)
		goto Finish;

	r = ext4_fs_get_inode_ref(&mp->fs, f.inode, &inode_ref);
	if (r != 0)
		goto Finish;

	*atime = ext4_inode_get_access_time(inode_ref.inode);
	r = ext4_fs_put_inode_ref(&inode_ref);

	Finish:
	EXT4_MP_UNLOCK(mp);

	return r;
}

int ext4_mtime_get(struct ext4_mountpoint *mp, const char *path, u32int *mtime)
{
	struct ext4_inode_ref inode_ref;
	ext4_file f;
	int r;

	EXT4_MP_LOCK(mp);

	r = ext4_generic_open2(mp, &f, path, O_RDONLY, EXT4_DE_UNKNOWN, nil);
	if (r != 0)
		goto Finish;

	r = ext4_fs_get_inode_ref(&mp->fs, f.inode, &inode_ref);
	if (r != 0)
		goto Finish;

	*mtime = ext4_inode_get_modif_time(inode_ref.inode);
	r = ext4_fs_put_inode_ref(&inode_ref);

	Finish:
	EXT4_MP_UNLOCK(mp);

	return r;
}

int ext4_ctime_get(struct ext4_mountpoint *mp, const char *path, u32int *ctime)
{
	struct ext4_inode_ref inode_ref;
	ext4_file f;
	int r;

	EXT4_MP_LOCK(mp);

	r = ext4_generic_open2(mp, &f, path, O_RDONLY, EXT4_DE_UNKNOWN, nil);
	if (r != 0)
		goto Finish;

	r = ext4_fs_get_inode_ref(&mp->fs, f.inode, &inode_ref);
	if (r != 0)
		goto Finish;

	*ctime = ext4_inode_get_change_inode_time(inode_ref.inode);
	r = ext4_fs_put_inode_ref(&inode_ref);

	Finish:
	EXT4_MP_UNLOCK(mp);

	return r;
}

static int ext4_fsymlink_set(ext4_file *f, const void *buf, u32int size)
{
	struct ext4_inode_ref ref;
	u32int sblock;
	ext4_fsblk_t fblock;
	u32int block_size;
	int r;

	assert(f && f->mp);

	if (!size)
		return 0;

	r = ext4_fs_get_inode_ref(&f->mp->fs, f->inode, &ref);
	if (r != 0)
		return r;

	/*Sync file size*/
	block_size = ext4_sb_get_block_size(&f->mp->fs.sb);
	if (size > block_size) {
		werrstr("invalid block size");
		r = -1;
		goto Finish;
	}
	r = ext4_ftruncate_no_lock(f, 0);
	if (r != 0)
		goto Finish;

	/*Start write back cache mode.*/
	r = ext4_block_cache_write_back(f->mp->fs.bdev, 1);
	if (r != 0)
		goto Finish;

	/*If the size of symlink is smaller than 60 bytes*/
	if (size < sizeof(ref.inode->blocks)) {
		memset(ref.inode->blocks, 0, sizeof(ref.inode->blocks));
		memcpy(ref.inode->blocks, buf, size);
		ext4_inode_clear_flag(ref.inode, EXT4_INODE_FLAG_EXTENTS);
	} else {
		u64int off;
		ext4_fs_inode_blocks_init(&f->mp->fs, &ref);
		r = ext4_fs_append_inode_dblk(&ref, &fblock, &sblock);
		if (r != 0)
			goto Finish;

		off = fblock * block_size;
		r = ext4_block_writebytes(f->mp->fs.bdev, off, buf, size);
		if (r != 0)
			goto Finish;
	}

	/*Stop write back cache mode*/
	ext4_block_cache_write_back(f->mp->fs.bdev, 0);

	if (r != 0)
		goto Finish;

	ext4_inode_set_size(ref.inode, size);
	ref.dirty = true;

	f->fsize = size;
	if (f->fpos > size)
		f->fpos = size;

Finish:
	ext4_fs_put_inode_ref(&ref);
	return r;
}

int ext4_fsymlink(struct ext4_mountpoint *mp, const char *target, const char *path)
{
	int r;
	ext4_file f;
	int filetype;

	if (mp->fs.read_only) {
		werrstr(Erdonlyfs);
		return -1;
	}

	filetype = EXT4_DE_SYMLINK;

	EXT4_MP_LOCK(mp);
	ext4_block_cache_write_back(mp->fs.bdev, 1);
	ext4_trans_start(mp);

	r = ext4_generic_open2(mp, &f, path, O_RDWR | O_CREAT, filetype, nil);
	if (r == 0)
		r = ext4_fsymlink_set(&f, target, strlen(target));
	else
		goto Finish;

	ext4_fclose(&f);

Finish:
	if (r != 0)
		ext4_trans_abort(mp);
	else
		ext4_trans_stop(mp);

	ext4_block_cache_write_back(mp->fs.bdev, 0);
	EXT4_MP_UNLOCK(mp);
	return r;
}

int ext4_readlink(struct ext4_mountpoint *mp, const char *path, char *buf, usize bufsize, usize *rcnt)
{
	int r;
	ext4_file f;
	int filetype;

	assert(buf != nil);

	filetype = EXT4_DE_SYMLINK;

	EXT4_MP_LOCK(mp);
	ext4_block_cache_write_back(mp->fs.bdev, 1);
	r = ext4_generic_open2(mp, &f, path, O_RDONLY, filetype, nil);
	if (r == 0)
		r = ext4_fread(&f, buf, bufsize, rcnt);
	else
		goto Finish;

	ext4_fclose(&f);

Finish:
	ext4_block_cache_write_back(mp->fs.bdev, 0);
	EXT4_MP_UNLOCK(mp);
	return r;
}

static int ext4_mknod_set(ext4_file *f, u32int dev)
{
	struct ext4_inode_ref ref;
	int r;

	assert(f && f->mp);

	r = ext4_fs_get_inode_ref(&f->mp->fs, f->inode, &ref);
	if (r != 0)
		return r;

	ext4_inode_set_dev(ref.inode, dev);

	ext4_inode_set_size(ref.inode, 0);
	ref.dirty = true;

	f->fsize = 0;
	f->fpos = 0;

	r = ext4_fs_put_inode_ref(&ref);
	return r;
}

int ext4_mknod(struct ext4_mountpoint *mp, const char *path, int filetype, u32int dev)
{
	int r;
	ext4_file f;

	if (mp->fs.read_only) {
		werrstr(Erdonlyfs);
		return -1;
	}
	/*
	 * The filetype shouldn't be normal file, directory or
	 * unknown.
	 */
	if (filetype == EXT4_DE_UNKNOWN ||
	    filetype == EXT4_DE_REG_FILE ||
	    filetype == EXT4_DE_DIR ||
	    filetype == EXT4_DE_SYMLINK) {
		werrstr(Einval);
		return -1;
	}

	/*
	 * Nor should it be any bogus value.
	 */
	if (filetype != EXT4_DE_CHRDEV &&
	    filetype != EXT4_DE_BLKDEV &&
	    filetype != EXT4_DE_FIFO &&
	    filetype != EXT4_DE_SOCK) {
		werrstr(Einval);
		return -1;
	}

	EXT4_MP_LOCK(mp);
	ext4_block_cache_write_back(mp->fs.bdev, 1);
	ext4_trans_start(mp);

	r = ext4_generic_open2(mp, &f, path, O_RDWR | O_CREAT, filetype, nil);
	if (r == 0) {
		if (filetype == EXT4_DE_CHRDEV ||
		    filetype == EXT4_DE_BLKDEV)
			r = ext4_mknod_set(&f, dev);
	} else {
		goto Finish;
	}

	ext4_fclose(&f);

Finish:
	if (r != 0)
		ext4_trans_abort(mp);
	else
		ext4_trans_stop(mp);

	ext4_block_cache_write_back(mp->fs.bdev, 0);
	EXT4_MP_UNLOCK(mp);
	return r;
}

/*********************************DIRECTORY OPERATION************************/

int ext4_dir_rm(struct ext4_mountpoint *mp, const char *path)
{
	int r;
	ext4_file f;

	struct ext4_inode_ref act;
	struct ext4_inode_ref child;
	struct ext4_dir_iter it;

	u32int inode_up;
	u32int inode_current;
	u32int depth = 1;

	bool has_children;
	bool dir_end;

	if (mp->fs.read_only) {
		werrstr(Erdonlyfs);
		return -1;
	}

	EXT4_MP_LOCK(mp);

	struct ext4_fs *const fs = &mp->fs;

	/*Check if exist.*/
	r = ext4_generic_open(mp, &f, path, "r", false, &inode_up);
	if (r != 0) {
		EXT4_MP_UNLOCK(mp);
		return r;
	}

	inode_current = f.inode;

	ext4_block_cache_write_back(mp->fs.bdev, 1);

	do {
		u64int act_curr_pos = 0;
		has_children = false;
		dir_end = false;

		while (r == 0 && !has_children && !dir_end) {

			/*Load directory node.*/
			r = ext4_fs_get_inode_ref(fs, inode_current, &act);
			if (r != 0) {
				break;
			}

			/*Initialize iterator.*/
			r = ext4_dir_iterator_init(&it, &act, act_curr_pos);
			if (r != 0) {
				ext4_fs_put_inode_ref(&act);
				break;
			}

			if (!it.curr) {
				dir_end = true;
				goto End;
			}

			ext4_trans_start(mp);

			/*Get up directory inode when ".." entry*/
			if ((it.curr->name_len == 2) &&
			    ext4_is_dots(it.curr->name, it.curr->name_len)) {
				inode_up = ext4_dir_en_get_inode(it.curr);
			}

			/*If directory or file entry,  but not "." ".." entry*/
			if (!ext4_is_dots(it.curr->name, it.curr->name_len)) {

				/*Get child inode reference do unlink
				 * directory/file.*/
				u32int cinode;
				u32int inode_type;
				cinode = ext4_dir_en_get_inode(it.curr);
				r = ext4_fs_get_inode_ref(fs, cinode, &child);
				if (r != 0)
					goto End;

				/*If directory with no leaf children*/
				r = ext4_has_children(&has_children, &child);
				if (r != 0) {
					ext4_fs_put_inode_ref(&child);
					goto End;
				}

				if (has_children) {
					/*Has directory children. Go into this
					 * directory.*/
					inode_up = inode_current;
					inode_current = cinode;
					depth++;
					ext4_fs_put_inode_ref(&child);
					goto End;
				}
				inode_type = ext4_inode_type(&mp->fs.sb,
						child.inode);

				/* Truncate */
				if (inode_type != EXT4_INODE_MODE_DIRECTORY)
					r = ext4_trunc_inode(mp, child.index, 0);
				else
					r = ext4_trunc_dir(mp, &act, &child);

				if (r != 0) {
					ext4_fs_put_inode_ref(&child);
					goto End;
				}

				/*No children in child directory or file. Just
				 * unlink.*/
				r = ext4_unlink(f.mp, &act, &child, (char *)it.curr->name);
				if (r != 0) {
					ext4_fs_put_inode_ref(&child);
					goto End;
				}

				ext4_inode_set_del_time(child.inode, -1L);
				ext4_inode_set_links_cnt(child.inode, 0);
				child.dirty = true;

				r = ext4_fs_free_inode(&child);
				if (r != 0) {
					ext4_fs_put_inode_ref(&child);
					goto End;
				}

				r = ext4_fs_put_inode_ref(&child);
				if (r != 0)
					goto End;

			}

			r = ext4_dir_iterator_next(&it);
			if (r != 0)
				goto End;

			act_curr_pos = it.curr_off;
End:
			ext4_dir_iterator_fini(&it);
			if (r == 0)
				r = ext4_fs_put_inode_ref(&act);
			else
				ext4_fs_put_inode_ref(&act);

			if (r != 0)
				ext4_trans_abort(mp);
			else
				ext4_trans_stop(mp);
		}

		if (dir_end) {
			/*Directory iterator reached last entry*/
			depth--;
			if (depth)
				inode_current = inode_up;

		}

		if (r != 0)
			break;

	} while (depth);

	/*Last unlink*/
	if (r == 0 && !depth) {
		/*Load parent.*/
		struct ext4_inode_ref parent;
		r = ext4_fs_get_inode_ref(&f.mp->fs, inode_up,
				&parent);
		if (r != 0)
			goto Finish;
		r = ext4_fs_get_inode_ref(&f.mp->fs, inode_current,
				&act);
		if (r != 0) {
			ext4_fs_put_inode_ref(&act);
			goto Finish;
		}

		ext4_trans_start(mp);

		/* In this place all directories should be
		 * unlinked.
		 * Last unlink from root of current directory*/
		r = ext4_unlink(f.mp, &parent, &act, (char *)path);
		if (r != 0) {
			ext4_fs_put_inode_ref(&parent);
			ext4_fs_put_inode_ref(&act);
			goto Finish;
		}

		if (ext4_inode_get_links_cnt(act.inode) == 2) {
			ext4_inode_set_del_time(act.inode, -1L);
			ext4_inode_set_links_cnt(act.inode, 0);
			act.dirty = true;
			/*Truncate*/
			r = ext4_trunc_dir(mp, &parent, &act);
			if (r != 0) {
				ext4_fs_put_inode_ref(&parent);
				ext4_fs_put_inode_ref(&act);
				goto Finish;
			}

			r = ext4_fs_free_inode(&act);
			if (r != 0) {
				ext4_fs_put_inode_ref(&parent);
				ext4_fs_put_inode_ref(&act);
				goto Finish;
			}
		}

		r = ext4_fs_put_inode_ref(&parent);
		if (r != 0)
			goto Finish;

		r = ext4_fs_put_inode_ref(&act);
	Finish:
		if (r != 0)
			ext4_trans_abort(mp);
		else
			ext4_trans_stop(mp);
	}

	ext4_block_cache_write_back(mp->fs.bdev, 0);
	EXT4_MP_UNLOCK(mp);

	return r;
}

int ext4_dir_mv(struct ext4_mountpoint *mp, const char *path, const char *new_path)
{
	return ext4_frename(mp, path, new_path);
}

int ext4_dir_mk(struct ext4_mountpoint *mp, const char *path)
{
	int r;
	ext4_file f;

	if (mp->fs.read_only) {
		werrstr(Erdonlyfs);
		return -1;
	}

	EXT4_MP_LOCK(mp);

	/*Check if exist.*/
	r = ext4_generic_open(mp, &f, path, "r", false, nil);
	if (r == 0) {
		werrstr(Eexists);
		r = -1;
		goto Finish;
	}

	/*Create new directory.*/
	r = ext4_generic_open(mp, &f, path, "w", false, nil);

Finish:
	EXT4_MP_UNLOCK(mp);
	return r;
}

int ext4_dir_open(struct ext4_mountpoint *mp, ext4_dir *dir, const char *path)
{
	int r;

	EXT4_MP_LOCK(mp);
	r = ext4_generic_open(mp, &dir->f, path, "r", false, nil);
	dir->next_off = 0;
	EXT4_MP_UNLOCK(mp);
	return r;
}

int ext4_dir_close(ext4_dir *dir)
{
    return ext4_fclose(&dir->f);
}

const ext4_direntry *ext4_dir_entry_next(ext4_dir *dir)
{
#define EXT4_DIR_ENTRY_OFFSET_TERM (u64int)(-1)

	int r;
	u16int name_length;
	ext4_direntry *de = 0;
	struct ext4_inode_ref dir_inode;
	struct ext4_dir_iter it;

	EXT4_MP_LOCK(dir->f.mp);

	if (dir->next_off == EXT4_DIR_ENTRY_OFFSET_TERM) {
		EXT4_MP_UNLOCK(dir->f.mp);
		return 0;
	}

	r = ext4_fs_get_inode_ref(&dir->f.mp->fs, dir->f.inode, &dir_inode);
	if (r != 0) {
		goto Finish;
	}

	r = ext4_dir_iterator_init(&it, &dir_inode, dir->next_off);
	if (r != 0) {
		ext4_fs_put_inode_ref(&dir_inode);
		goto Finish;
	}

	memset(dir->de.name, 0, sizeof(dir->de.name));
	name_length = ext4_dir_en_get_name_len(&dir->f.mp->fs.sb,
					       it.curr);
	memcpy(dir->de.name, it.curr->name, name_length);

	/* Directly copying the content isn't safe for Big-endian targets*/
	dir->de.inode = ext4_dir_en_get_inode(it.curr);
	dir->de.entry_length = ext4_dir_en_get_entry_len(it.curr);
	dir->de.name_length = name_length;
	dir->de.inode_type = ext4_dir_en_get_inode_type(&dir->f.mp->fs.sb,
						      it.curr);

	de = &dir->de;

	ext4_dir_iterator_next(&it);

	dir->next_off = it.curr ? it.curr_off : EXT4_DIR_ENTRY_OFFSET_TERM;

	ext4_dir_iterator_fini(&it);
	ext4_fs_put_inode_ref(&dir_inode);

Finish:
	EXT4_MP_UNLOCK(dir->f.mp);
	return de;
}

void ext4_dir_entry_rewind(ext4_dir *dir)
{
	dir->next_off = 0;
}
