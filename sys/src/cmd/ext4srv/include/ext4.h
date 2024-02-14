#pragma once

#include "ext4_types.h"
#include "ext4_debug.h"
#include "ext4_blockdev.h"
#include "ext4_fs.h"
#include "ext4_journal.h"

/**@brief   Mount point descriptor.*/
struct ext4_mountpoint {

	/**@brief   Mount done flag.*/
	bool mounted;

	/**@brief   OS dependent lock/unlock functions.*/
	const struct ext4_lock *os_locks;

	/**@brief   Ext4 filesystem internals.*/
	struct ext4_fs fs;

	/**@brief   JBD fs.*/
	struct jbd_fs jbd_fs;

	/**@brief   Journal.*/
	struct jbd_journal jbd_journal;

	/**@brief   Block cache.*/
	struct ext4_bcache bc;
};

/********************************OS LOCK INFERFACE***************************/

/**@brief   OS dependent lock interface.*/
struct ext4_lock {

	/**@brief   Lock access to mount point.*/
	void (*lock)(void *aux);

	/**@brief   Unlock access to mount point.*/
	void (*unlock)(void *aux);

	/**@brief   Auxilary pointer.*/
	void *p_user;
};

/********************************FILE DESCRIPTOR*****************************/

/**@brief   File descriptor. */
typedef struct ext4_file {

	/**@brief   Mount point handle.*/
	struct ext4_mountpoint *mp;

	/**@brief   File inode id.*/
	u32int inode;

	/**@brief   Open flags.*/
	u32int flags;

	/**@brief   File size.*/
	u64int fsize;

	/**@brief   Actual file position.*/
	u64int fpos;
} ext4_file;

/*****************************DIRECTORY DESCRIPTOR***************************/

/**@brief   Directory entry descriptor. */
typedef struct ext4_direntry {
	u32int inode;
	u16int entry_length;
	u8int name_length;
	u8int inode_type;
	u8int name[255];
} ext4_direntry;

/**@brief   Directory descriptor. */
typedef struct ext4_dir {
	/**@brief   File descriptor.*/
	ext4_file f;
	/**@brief   Current directory entry.*/
	ext4_direntry de;
	/**@brief   Next entry offset.*/
	u64int next_off;
} ext4_dir;

/********************************MOUNT OPERATIONS****************************/

int ext4_mount(struct ext4_mountpoint *mp, struct ext4_blockdev *bd, bool read_only);
int ext4_umount(struct ext4_mountpoint *mp);
int ext4_journal_start(struct ext4_mountpoint *mp);
int ext4_journal_stop(struct ext4_mountpoint *mp);
int ext4_recover(struct ext4_mountpoint *mp);

/**@brief   Some of the filesystem stats. */
struct ext4_mount_stats {
	u32int inodes_count;
	u32int free_inodes_count;
	u64int blocks_count;
	u64int free_blocks_count;

	u32int block_size;
	u32int block_group_count;
	u32int blocks_per_group;
	u32int inodes_per_group;

	char volume_name[16];
};

int ext4_mount_point_stats(struct ext4_mountpoint *mp, struct ext4_mount_stats *stats);
int ext4_mount_setup_locks(struct ext4_mountpoint *mp, const struct ext4_lock *locks);
int ext4_get_sblock(struct ext4_mountpoint *mp, struct ext4_sblock **sb);
int ext4_cache_write_back(struct ext4_mountpoint *mp, bool on);
int ext4_cache_flush(struct ext4_mountpoint *mp);

/********************************FILE OPERATIONS*****************************/

/**@brief   Remove file by path.
 *
 * @param   path Path to file.
 *
 * @return  Standard error code. */
int ext4_fremove(struct ext4_mountpoint *mp, const char *path);

/**@brief   Create a hardlink for a file.
 *
 * @param   path Path to file.
 * @param   hardlink_path Path of hardlink.
 *
 * @return  Standard error code. */
int ext4_flink(struct ext4_mountpoint *mp, const char *path, const char *hardlink_path);

/**@brief Rename file.
 * @param path Source.
 * @param new_path Destination.
 * @return  Standard error code. */
int ext4_frename(struct ext4_mountpoint *mp, const char *path, const char *new_path);

/**@brief   File open function.
 *
 * @param   file  File handle.
 * @param   path  File path, has to start from mount point:/my_partition/file.
 * @param   flags File open flags.
 *  |---------------------------------------------------------------|
 *  |   r or rb                 O_RDONLY                            |
 *  |---------------------------------------------------------------|
 *  |   w or wb                 O_WRONLY|O_CREAT|O_TRUNC            |
 *  |---------------------------------------------------------------|
 *  |   a or ab                 O_WRONLY|O_CREAT|O_APPEND           |
 *  |---------------------------------------------------------------|
 *  |   r+ or rb+ or r+b        O_RDWR                              |
 *  |---------------------------------------------------------------|
 *  |   w+ or wb+ or w+b        O_RDWR|O_CREAT|O_TRUNC              |
 *  |---------------------------------------------------------------|
 *  |   a+ or ab+ or a+b        O_RDWR|O_CREAT|O_APPEND             |
 *  |---------------------------------------------------------------|
 *
 * @return  Standard error code.*/
int ext4_fopen(struct ext4_mountpoint *mp, ext4_file *file, const char *path, const char *flags);

/**@brief   Alternate file open function.
 *
 * @param   file  File handle.
 * @param   path  File path, has to start from mount point:/my_partition/file.
 * @param   flags File open flags.
 *
 * @return  Standard error code.*/
int ext4_fopen2(struct ext4_mountpoint *mp, ext4_file *file, const char *path, int flags);

/**@brief   File close function.
 *
 * @param   file File handle.
 *
 * @return  Standard error code.*/
int ext4_fclose(ext4_file *file);


/**@brief   File truncate function.
 *
 * @param   file File handle.
 * @param   size New file size.
 *
 * @return  Standard error code.*/
int ext4_ftruncate(ext4_file *file, u64int size);

/**@brief   Read data from file.
 *
 * @param   file File handle.
 * @param   buf  Output buffer.
 * @param   size Bytes to read.
 * @param   rcnt Bytes read (nil allowed).
 *
 * @return  Standard error code.*/
int ext4_fread(ext4_file *file, void *buf, usize size, usize *rcnt);

/**@brief   Write data to file.
 *
 * @param   file File handle.
 * @param   buf  Data to write
 * @param   size Write length..
 * @param   wcnt Bytes written (nil allowed).
 *
 * @return  Standard error code.*/
int ext4_fwrite(ext4_file *file, const void *buf, usize size, usize *wcnt);

/**@brief   File seek operation.
 *
 * @param   file File handle.
 * @param   offset Offset to seek.
 * @param   origin Seek type:
 *              @ref 0 (set)
 *              @ref 1 (cur)
 *              @ref 2 (end)
 *
 * @return  Standard error code.*/
int ext4_fseek(ext4_file *file, s64int offset, int origin);

/**@brief   Get file position.
 *
 * @param   file File handle.
 *
 * @return  Actual file position */
u64int ext4_ftell(ext4_file *file);

/**@brief   Get file size.
 *
 * @param   file File handle.
 *
 * @return  File size. */
u64int ext4_fsize(ext4_file *file);


/**@brief Get inode of file/directory/link.
 *
 * @param path    Parh to file/dir/link.
 * @param ret_ino Inode number.
 * @param inode   Inode internals.
 *
 * @return  Standard error code.*/
int ext4_raw_inode_fill(struct ext4_mountpoint *mp, const char *path, u32int *ret_ino,
			struct ext4_inode *inode);

/**@brief Check if inode exists.
 *
 * @param path    Parh to file/dir/link.
 * @param type    Inode type.
 *                @ref EXT4_DE_UNKNOWN
 *                @ref EXT4_DE_REG_FILE
 *                @ref EXT4_DE_DIR
 *                @ref EXT4_DE_CHRDEV
 *                @ref EXT4_DE_BLKDEV
 *                @ref EXT4_DE_FIFO
 *                @ref EXT4_DE_SOCK
 *                @ref EXT4_DE_SYMLINK
 *
 * @return  Standard error code.*/
int ext4_inode_exist(struct ext4_mountpoint *mp, const char *path, int type);

/**@brief Change file/directory/link mode bits.
 *
 * @param path Path to file/dir/link.
 * @param mode New mode bits (for example 0777).
 *
 * @return  Standard error code.*/
int ext4_mode_set(struct ext4_mountpoint *mp, const char *path, u32int mode);


/**@brief Get file/directory/link mode bits.
 *
 * @param path Path to file/dir/link.
 * @param mode New mode bits (for example 0777).
 *
 * @return  Standard error code.*/
int ext4_mode_get(struct ext4_mountpoint *mp, const char *path, u32int *mode);

/**@brief Change file owner and group.
 *
 * @param path Path to file/dir/link.
 * @param uid  User id.
 * @param gid  Group id.
 *
 * @return  Standard error code.*/
int ext4_owner_set(struct ext4_mountpoint *mp, const char *path, u32int uid, u32int gid);

/**@brief Get file/directory/link owner and group.
 *
 * @param path Path to file/dir/link.
 * @param uid  User id.
 * @param gid  Group id.
 *
 * @return  Standard error code.*/
int ext4_owner_get(struct ext4_mountpoint *mp, const char *path, u32int *uid, u32int *gid);

/**@brief Set file/directory/link access time.
 *
 * @param path  Path to file/dir/link.
 * @param atime Access timestamp.
 *
 * @return  Standard error code.*/
int ext4_atime_set(struct ext4_mountpoint *mp, const char *path, u32int atime);

/**@brief Set file/directory/link modify time.
 *
 * @param path  Path to file/dir/link.
 * @param mtime Modify timestamp.
 *
 * @return  Standard error code.*/
int ext4_mtime_set(struct ext4_mountpoint *mp, const char *path, u32int mtime);

/**@brief Set file/directory/link change time.
 *
 * @param path  Path to file/dir/link.
 * @param ctime Change timestamp.
 *
 * @return  Standard error code.*/
int ext4_ctime_set(struct ext4_mountpoint *mp, const char *path, u32int ctime);

/**@brief Get file/directory/link access time.
 *
 * @param path  Path to file/dir/link.
 * @param atime Access timestamp.
 *
 * @return  Standard error code.*/
int ext4_atime_get(struct ext4_mountpoint *mp, const char *path, u32int *atime);

/**@brief Get file/directory/link modify time.
 *
 * @param path  Path to file/dir/link.
 * @param mtime Modify timestamp.
 *
 * @return  Standard error code.*/
int ext4_mtime_get(struct ext4_mountpoint *mp, const char *path, u32int *mtime);

/**@brief Get file/directory/link change time.
 *
 * @param path  Pathto file/dir/link.
 * @param ctime Change timestamp.
 *
 * @return  standard error code*/
int ext4_ctime_get(struct ext4_mountpoint *mp, const char *path, u32int *ctime);

/**@brief Create symbolic link.
 *
 * @param target Destination entry path.
 * @param path   Source entry path.
 *
 * @return  Standard error code.*/
int ext4_fsymlink(struct ext4_mountpoint *mp, const char *target, const char *path);

/**@brief Create special file.
 * @param path     Path to new special file.
 * @param filetype Filetype of the new special file.
 * 	           (that must not be regular file, directory, or unknown type)
 * @param dev      If filetype is char device or block device,
 * 	           the device number will become the payload in the inode.
 * @return  Standard error code.*/
int ext4_mknod(struct ext4_mountpoint *mp, const char *path, int filetype, u32int dev);

/**@brief Read symbolic link payload.
 *
 * @param path    Path to symlink.
 * @param buf     Output buffer.
 * @param bufsize Output buffer max size.
 * @param rcnt    Bytes read.
 *
 * @return  Standard error code.*/
int ext4_readlink(struct ext4_mountpoint *mp, const char *path, char *buf, usize bufsize, usize *rcnt);

/*********************************DIRECTORY OPERATION***********************/

/**@brief   Recursive directory remove.
 *
 * @param   path Directory path to remove
 *
 * @return  Standard error code.*/
int ext4_dir_rm(struct ext4_mountpoint *mp, const char *path);

/**@brief Rename/move directory.
 *
 * @param path     Source path.
 * @param new_path Destination path.
 *
 * @return  Standard error code. */
int ext4_dir_mv(struct ext4_mountpoint *mp, const char *path, const char *new_path);

/**@brief   Create new directory.
 *
 * @param   path Directory name.
 *
 * @return  Standard error code.*/
int ext4_dir_mk(struct ext4_mountpoint *mp, const char *path);

/**@brief   Directory open.
 *
 * @param   dir  Directory handle.
 * @param   path Directory path.
 *
 * @return  Standard error code.*/
int ext4_dir_open(struct ext4_mountpoint *mp, ext4_dir *dir, const char *path);

/**@brief   Directory close.
 *
 * @param   dir directory handle.
 *
 * @return  Standard error code.*/
int ext4_dir_close(ext4_dir *dir);

/**@brief   Return next directory entry.
 *
 * @param   dir Directory handle.
 *
 * @return  Directory entry id (nil if no entry)*/
const ext4_direntry *ext4_dir_entry_next(ext4_dir *dir);

/**@brief   Rewind directory entry offset.
 *
 * @param   dir Directory handle.*/
void ext4_dir_entry_rewind(ext4_dir *dir);
