#pragma once

#include "ext4_config.h"

#define DEBUG_BALLOC (1ul << 0)
#define DEBUG_BCACHE (1ul << 1)
#define DEBUG_BITMAP (1ul << 2)
#define DEBUG_BLOCK_GROUP (1ul << 3)
#define DEBUG_BLOCKDEV (1ul << 4)
#define DEBUG_DIR_IDX (1ul << 5)
#define DEBUG_DIR (1ul << 6)
#define DEBUG_EXTENT (1ul << 7)
#define DEBUG_FS (1ul << 8)
#define DEBUG_HASH (1ul << 9)
#define DEBUG_IALLOC (1ul << 10)
#define DEBUG_INODE (1ul << 11)
#define DEBUG_SUPER (1ul << 12)
#define DEBUG_XATTR (1ul << 13)
#define DEBUG_MKFS (1ul << 14)
#define DEBUG_EXT4 (1ul << 15)
#define DEBUG_JBD (1ul << 16)
#define DEBUG_MBR (1ul << 17)

#define DEBUG_NOPREFIX (1ul << 31)
#define DEBUG_ALL (0xFFFFFFFF)

static inline const char *ext4_dmask_id2str(u32int m)
{
	switch(m) {
	case DEBUG_BALLOC:
		return "ext4_balloc: ";
	case DEBUG_BCACHE:
		return "ext4_bcache: ";
	case DEBUG_BITMAP:
		return "ext4_bitmap: ";
	case DEBUG_BLOCK_GROUP:
		return "ext4_block_group: ";
	case DEBUG_BLOCKDEV:
		return "ext4_blockdev: ";
	case DEBUG_DIR_IDX:
		return "ext4_dir_idx: ";
	case DEBUG_DIR:
		return "ext4_dir: ";
	case DEBUG_EXTENT:
		return "ext4_extent: ";
	case DEBUG_FS:
		return "ext4_fs: ";
	case DEBUG_HASH:
		return "ext4_hash: ";
	case DEBUG_IALLOC:
		return "ext4_ialloc: ";
	case DEBUG_INODE:
		return "ext4_inode: ";
	case DEBUG_SUPER:
		return "ext4_super: ";
	case DEBUG_MKFS:
		return "ext4_mkfs: ";
	case DEBUG_JBD:
		return "ext4_jbd: ";
	case DEBUG_MBR:
		return "ext4_mbr: ";
	case DEBUG_EXT4:
		return "ext4: ";
	}
	return "";
}
#define DBG_NONE  ""
#define DBG_INFO  "[info]  "
#define DBG_WARN  "[warn]  "
#define DBG_ERROR "[error] "

/**@brief   Global mask debug set.
 * @brief   m new debug mask.*/
void ext4_dmask_set(u32int m);

/**@brief   Global mask debug clear.
 * @brief   m new debug mask.*/
void ext4_dmask_clr(u32int m);

/**@brief   Global debug mask get.
 * @return  debug mask*/
u32int ext4_dmask_get(void);

/**@brief   Debug printf.*/
#define ext4_dbg(m, ...) \
	do { \
		if ((m) & ext4_dmask_get()) { \
			if (!((m) & DEBUG_NOPREFIX)) { \
				fprint(2, "%s: %s", __func__, ext4_dmask_id2str(m)); \
			} \
			fprint(2, __VA_ARGS__); \
		} \
	} while (0)
