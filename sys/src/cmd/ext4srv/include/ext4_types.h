#pragma once

#include "ext4_blockdev.h"
#include "tree.h"

/*
 * Types of blocks.
 */
typedef u32int ext4_lblk_t;
typedef u64int ext4_fsblk_t;

#define EXT4_CHECKSUM_CRC32C 1

#define UUID_SIZE 16

#pragma pack on

/*
 * Structure of the super block
 */
struct ext4_sblock {
	u32int inodes_count;		   /* I-nodes count */
	u32int blocks_count_lo;	  /* Blocks count */
	u32int reserved_blocks_count_lo; /* Reserved blocks count */
	u32int free_blocks_count_lo;     /* Free blocks count */
	u32int free_inodes_count;	/* Free inodes count */
	u32int first_data_block;	 /* First Data Block */
	u32int log_block_size;	   /* Block size */
	u32int log_cluster_size;	 /* Obsoleted fragment size */
	u32int blocks_per_group;	 /* Number of blocks per group */
	u32int frags_per_group;	  /* Obsoleted fragments per group */
	u32int inodes_per_group;	 /* Number of inodes per group */
	u32int mount_time;		   /* Mount time */
	u32int write_time;		   /* Write time */
	u16int mount_count;		   /* Mount count */
	u16int max_mount_count;	  /* Maximal mount count */
	u16int magic;			   /* Magic signature */
	u16int state;			   /* File system state */
	u16int errors;		   /* Behavior when detecting errors */
	u16int minor_rev_level;	  /* Minor revision level */
	u32int last_check_time;	  /* Time of last check */
	u32int check_interval;	   /* Maximum time between checks */
	u32int creator_os;		   /* Creator OS */
	u32int rev_level;		   /* Revision level */
	u16int def_resuid;		   /* Default uid for reserved blocks */
	u16int def_resgid;		   /* Default gid for reserved blocks */

	/* Fields for EXT4_DYNAMIC_REV superblocks only. */
	u32int first_inode;	 /* First non-reserved inode */
	u16int inode_size;	  /* Size of inode structure */
	u16int block_group_index;   /* Block group index of this superblock */
	u32int features_compatible; /* Compatible feature set */
	u32int features_incompatible;  /* Incompatible feature set */
	u32int features_read_only;     /* Readonly-compatible feature set */
	u8int uuid[UUID_SIZE];		 /* 128-bit uuid for volume */
	char volume_name[16];		 /* Volume name */
	char last_mounted[64];		 /* Directory where last mounted */
	u32int algorithm_usage_bitmap; /* For compression */

	/*
	 * Performance hints. Directory preallocation should only
	 * happen if the EXT4_FEATURE_COMPAT_DIR_PREALLOC flag is on.
	 */
	u8int s_prealloc_blocks; /* Number of blocks to try to preallocate */
	u8int s_prealloc_dir_blocks;  /* Number to preallocate for dirs */
	u16int s_reserved_gdt_blocks; /* Per group desc for online growth */

	/*
	 * Journaling support valid if EXT4_FEATURE_COMPAT_HAS_JOURNAL set.
	 */
	u8int journal_uuid[UUID_SIZE];      /* UUID of journal superblock */
	u32int journal_inode_number; /* Inode number of journal file */
	u32int journal_dev;	  /* Device number of journal file */
	u32int last_orphan;	  /* Head of list of inodes to delete */
	u32int hash_seed[4];	 /* HTREE hash seed */
	u8int default_hash_version;  /* Default hash version to use */
	u8int journal_backup_type;
	u16int desc_size;	  /* Size of group descriptor */
	u32int default_mount_opts; /* Default mount options */
	u32int first_meta_bg;      /* First metablock block group */
	u32int mkfs_time;	  /* When the filesystem was created */
	u32int journal_blocks[17]; /* Backup of the journal inode */

	/* 64bit support valid if EXT4_FEATURE_COMPAT_64BIT */
	u32int blocks_count_hi;	  /* Blocks count */
	u32int reserved_blocks_count_hi; /* Reserved blocks count */
	u32int free_blocks_count_hi;     /* Free blocks count */
	u16int min_extra_isize;    /* All inodes have at least # bytes */
	u16int want_extra_isize;   /* New inodes should reserve # bytes */
	u32int flags;		     /* Miscellaneous flags */
	u16int raid_stride;	/* RAID stride */
	u16int mmp_interval;       /* # seconds to wait in MMP checking */
	u64int mmp_block;	  /* Block for multi-mount protection */
	u32int raid_stripe_width;  /* Blocks on all data disks (N * stride) */
	u8int log_groups_per_flex; /* FLEX_BG group size */
	u8int checksum_type;
	u16int reserved_pad;
	u64int kbytes_written; /* Number of lifetime kilobytes written */
	u32int snapshot_inum;  /* I-node number of active snapshot */
	u32int snapshot_id;    /* Sequential ID of active snapshot */
	u64int
	    snapshot_r_blocks_count; /* Reserved blocks for active snapshot's
					future use */
	u32int
	    snapshot_list; /* I-node number of the head of the on-disk snapshot
			      list */
	u32int error_count;	 /* Number of file system errors */
	u32int first_error_time;    /* First time an error happened */
	u32int first_error_ino;     /* I-node involved in first error */
	u64int first_error_block;   /* Block involved of first error */
	u8int first_error_func[32]; /* Function where the error happened */
	u32int first_error_line;    /* Line number where error happened */
	u32int last_error_time;     /* Most recent time of an error */
	u32int last_error_ino;      /* I-node involved in last error */
	u32int last_error_line;     /* Line number where error happened */
	u64int last_error_block;    /* Block involved of last error */
	u8int last_error_func[32];  /* Function where the error happened */
	u8int mount_opts[64];
	u32int usr_quota_inum;	/* inode for tracking user quota */
	u32int grp_quota_inum;	/* inode for tracking group quota */
	u32int overhead_clusters;	/* overhead blocks/clusters in fs */
	u32int backup_bgs[2];	/* groups with sparse_super2 SBs */
	u8int  encrypt_algos[4];	/* Encryption algorithms in use  */
	u8int  encrypt_pw_salt[16];	/* Salt used for string2key algorithm */
	u32int lpf_ino;		/* Location of the lost+found inode */
	u32int padding[100];	/* Padding to the end of the block */
	u32int checksum;		/* crc32c(superblock) */
};

#pragma pack off

#define EXT4_SUPERBLOCK_MAGIC 0xEF53
#define EXT4_SUPERBLOCK_SIZE 1024
#define EXT4_SUPERBLOCK_OFFSET 1024

#define EXT4_SUPERBLOCK_OS_LINUX 0
#define EXT4_SUPERBLOCK_OS_HURD 1

/*
 * Misc. filesystem flags
 */
#define EXT4_SUPERBLOCK_FLAGS_SIGNED_HASH 0x0001
#define EXT4_SUPERBLOCK_FLAGS_UNSIGNED_HASH 0x0002
#define EXT4_SUPERBLOCK_FLAGS_TEST_FILESYS 0x0004
/*
 * Filesystem states
 */
#define EXT4_SUPERBLOCK_STATE_VALID_FS 0x0001  /* Unmounted cleanly */
#define EXT4_SUPERBLOCK_STATE_ERROR_FS 0x0002  /* Errors detected */
#define EXT4_SUPERBLOCK_STATE_ORPHAN_FS 0x0004 /* Orphans being recovered */

/*
 * Behaviour when errors detected
 */
#define EXT4_SUPERBLOCK_ERRORS_CONTINUE 1 /* Continue execution */
#define EXT4_SUPERBLOCK_ERRORS_RO 2       /* Remount fs read-only */
#define EXT4_SUPERBLOCK_ERRORS_PANIC 3    /* Panic */
#define EXT4_SUPERBLOCK_ERRORS_DEFAULT EXT4_ERRORS_CONTINUE

/*
 * Compatible features
 */
#define EXT4_FCOM_DIR_PREALLOC 0x0001
#define EXT4_FCOM_IMAGIC_INODES 0x0002
#define EXT4_FCOM_HAS_JOURNAL 0x0004
#define EXT4_FCOM_EXT_ATTR 0x0008
#define EXT4_FCOM_RESIZE_INODE 0x0010
#define EXT4_FCOM_DIR_INDEX 0x0020

/*
 * Read-only compatible features
 */
#define EXT4_FRO_COM_SPARSE_SUPER 0x0001
#define EXT4_FRO_COM_LARGE_FILE 0x0002
#define EXT4_FRO_COM_BTREE_DIR 0x0004
#define EXT4_FRO_COM_HUGE_FILE 0x0008
#define EXT4_FRO_COM_GDT_CSUM 0x0010
#define EXT4_FRO_COM_DIR_NLINK 0x0020
#define EXT4_FRO_COM_EXTRA_ISIZE 0x0040
#define EXT4_FRO_COM_QUOTA 0x0100
#define EXT4_FRO_COM_BIGALLOC 0x0200
#define EXT4_FRO_COM_METADATA_CSUM 0x0400

/*
 * Incompatible features
 */
#define EXT4_FINCOM_COMPRESSION 0x0001
#define EXT4_FINCOM_FILETYPE 0x0002
#define EXT4_FINCOM_RECOVER 0x0004     /* Needs recovery */
#define EXT4_FINCOM_JOURNAL_DEV 0x0008 /* Journal device */
#define EXT4_FINCOM_META_BG 0x0010
#define EXT4_FINCOM_EXTENTS 0x0040 /* extents support */
#define EXT4_FINCOM_64BIT 0x0080
#define EXT4_FINCOM_MMP 0x0100
#define EXT4_FINCOM_FLEX_BG 0x0200
#define EXT4_FINCOM_EA_INODE 0x0400	 /* EA in inode */
#define EXT4_FINCOM_DIRDATA 0x1000	  /* data in dirent */
#define EXT4_FINCOM_BG_USE_META_CSUM 0x2000 /* use crc32c for bg */
#define EXT4_FINCOM_LARGEDIR 0x4000	 /* >2GB or 3-lvl htree */
#define EXT4_FINCOM_INLINE_DATA 0x8000      /* data in inode */

/*
 * EXT2 supported feature set
 */
#define EXT2_SUPPORTED_FCOM 0x0000

#define EXT2_SUPPORTED_FINCOM                                   \
	(EXT4_FINCOM_FILETYPE | EXT4_FINCOM_META_BG)

#define EXT2_SUPPORTED_FRO_COM                                  \
	(EXT4_FRO_COM_SPARSE_SUPER |                            \
	 EXT4_FRO_COM_LARGE_FILE)

/*
 * EXT3 supported feature set
 */
#define EXT3_SUPPORTED_FCOM (EXT4_FCOM_DIR_INDEX)

#define EXT3_SUPPORTED_FINCOM                                 \
	(EXT4_FINCOM_FILETYPE | EXT4_FINCOM_META_BG)

#define EXT3_SUPPORTED_FRO_COM                                \
	(EXT4_FRO_COM_SPARSE_SUPER | EXT4_FRO_COM_LARGE_FILE)

/*
 * EXT4 supported feature set
 */
#define EXT4_SUPPORTED_FCOM (EXT4_FCOM_DIR_INDEX)

#define EXT4_SUPPORTED_FINCOM ( \
	EXT4_FINCOM_FILETYPE | EXT4_FINCOM_META_BG |      \
	EXT4_FINCOM_EXTENTS | EXT4_FINCOM_FLEX_BG |       \
	EXT4_FINCOM_64BIT \
)

#define EXT4_SUPPORTED_FRO_COM ( \
	EXT4_FRO_COM_SPARSE_SUPER |                       \
	EXT4_FRO_COM_METADATA_CSUM |                      \
	EXT4_FRO_COM_LARGE_FILE | EXT4_FRO_COM_GDT_CSUM | \
	EXT4_FRO_COM_DIR_NLINK |                          \
	EXT4_FRO_COM_EXTRA_ISIZE | EXT4_FRO_COM_HUGE_FILE \
)

/*Ignored features:
 * RECOVER - journaling in lwext4 is not supported
 *           (probably won't be ever...)
 * MMP - multi-mout protection (impossible scenario)
 * */
#define EXT_FINCOM_IGNORED ( \
	EXT4_FINCOM_RECOVER | \
	EXT4_FINCOM_MMP | \
	EXT4_FINCOM_BG_USE_META_CSUM \
)

/*
// TODO: Features incompatible to implement
#define EXT4_SUPPORTED_FINCOM
                     (EXT4_FINCOM_INLINE_DATA)

// TODO: Features read only to implement
#define EXT4_SUPPORTED_FRO_COM
                     EXT4_FRO_COM_BIGALLOC |\
                     EXT4_FRO_COM_QUOTA)
*/


/* Inode table/bitmap not in use */
#define EXT4_BLOCK_GROUP_INODE_UNINIT 0x0001
/* Block bitmap not in use */
#define EXT4_BLOCK_GROUP_BLOCK_UNINIT 0x0002
/* On-disk itable initialized to zero */
#define EXT4_BLOCK_GROUP_ITABLE_ZEROED 0x0004

#pragma pack on

/*
 * Structure of a blocks group descriptor
 */
struct ext4_bgroup {
	u32int block_bitmap_lo;	    /* Blocks bitmap block */
	u32int inode_bitmap_lo;	    /* Inodes bitmap block */
	u32int inode_table_first_block_lo; /* Inodes table block */
	u16int free_blocks_count_lo;       /* Free blocks count */
	u16int free_inodes_count_lo;       /* Free inodes count */
	u16int used_dirs_count_lo;	 /* Directories count */
	u16int flags;		       /* EXT4_BG_flags (INODE_UNINIT, etc) */
	u32int exclude_bitmap_lo;    /* Exclude bitmap for snapshots */
	u16int block_bitmap_csum_lo; /* crc32c(s_uuid+grp_num+bbitmap) LE */
	u16int inode_bitmap_csum_lo; /* crc32c(s_uuid+grp_num+ibitmap) LE */
	u16int itable_unused_lo;     /* Unused inodes count */
	u16int checksum;	     /* crc16(sb_uuid+group+desc) */

	u32int block_bitmap_hi;	    /* Blocks bitmap block MSB */
	u32int inode_bitmap_hi;	    /* I-nodes bitmap block MSB */
	u32int inode_table_first_block_hi; /* I-nodes table block MSB */
	u16int free_blocks_count_hi;       /* Free blocks count MSB */
	u16int free_inodes_count_hi;       /* Free i-nodes count MSB */
	u16int used_dirs_count_hi;	 /* Directories count MSB */
	u16int itable_unused_hi;	   /* Unused inodes count MSB */
	u32int exclude_bitmap_hi;	  /* Exclude bitmap block MSB */
	u16int block_bitmap_csum_hi; /* crc32c(s_uuid+grp_num+bbitmap) BE */
	u16int inode_bitmap_csum_hi; /* crc32c(s_uuid+grp_num+ibitmap) BE */
	u32int reserved;	     /* Padding */
};

#pragma pack off

#define EXT4_MIN_BLOCK_GROUP_DESCRIPTOR_SIZE 32
#define EXT4_MAX_BLOCK_GROUP_DESCRIPTOR_SIZE 64

#define EXT4_MIN_BLOCK_SIZE 1024  /* 1 KiB */
#define EXT4_MAX_BLOCK_SIZE 65536 /* 64 KiB */
#define EXT4_REV0_INODE_SIZE 128

#define EXT4_INODE_BLOCK_SIZE 512

#define EXT4_INODE_DIRECT_BLOCK_COUNT 12
#define EXT4_INODE_INDIRECT_BLOCK EXT4_INODE_DIRECT_BLOCK_COUNT
#define EXT4_INODE_DOUBLE_INDIRECT_BLOCK (EXT4_INODE_INDIRECT_BLOCK + 1)
#define EXT4_INODE_TRIPPLE_INDIRECT_BLOCK (EXT4_INODE_DOUBLE_INDIRECT_BLOCK + 1)
#define EXT4_INODE_BLOCKS (EXT4_INODE_TRIPPLE_INDIRECT_BLOCK + 1)
#define EXT4_INODE_INDIRECT_BLOCK_COUNT                                        \
	(EXT4_INODE_BLOCKS - EXT4_INODE_DIRECT_BLOCK_COUNT)

#pragma pack on

/*
 * Structure of an inode on the disk
 */
struct ext4_inode {
	u16int mode;		    /* File mode */
	u16int uid;		    /* Low 16 bits of owner uid */
	u32int size_lo;	   /* Size in bytes */
	u32int access_time;       /* Access time */
	u32int change_inode_time; /* I-node change time */
	u32int modification_time; /* Modification time */
	u32int deletion_time;     /* Deletion time */
	u16int gid;		    /* Low 16 bits of group id */
	u16int links_count;       /* Links count */
	u32int blocks_count_lo;   /* Blocks count */
	u32int flags;		    /* File flags */
	u32int unused_osd1;       /* OS dependent - not used in HelenOS */
	u32int blocks[EXT4_INODE_BLOCKS]; /* Pointers to blocks */
	u32int generation;		    /* File version (for NFS) */
	u32int file_acl_lo;		    /* File ACL */
	u32int size_hi;
	u32int obso_faddr; /* Obsoleted fragment address */

	union {
		struct {
			u16int blocks_high;
			u16int file_acl_high;
			u16int uid_high;
			u16int gid_high;
			u16int checksum_lo; /* crc32c(uuid+inum+inode) LE */
			u16int reserved2;
		} linux2;
		struct {
			u16int reserved1;
			u16int mode_high;
			u16int uid_high;
			u16int gid_high;
			u32int author;
		} hurd2;
	} osd2;

	u16int extra_isize;
	u16int checksum_hi;	/* crc32c(uuid+inum+inode) BE */
	u32int ctime_extra; /* Extra change time (nsec << 2 | epoch) */
	u32int mtime_extra; /* Extra Modification time (nsec << 2 | epoch) */
	u32int atime_extra; /* Extra Access time (nsec << 2 | epoch) */
	u32int crtime;      /* File creation time */
	u32int
	    crtime_extra;    /* Extra file creation time (nsec << 2 | epoch) */
	u32int version_hi; /* High 32 bits for 64-bit version */
};

#pragma pack off

#define EXT4_INODE_MODE_FIFO 0x1000
#define EXT4_INODE_MODE_CHARDEV 0x2000
#define EXT4_INODE_MODE_DIRECTORY 0x4000
#define EXT4_INODE_MODE_BLOCKDEV 0x6000
#define EXT4_INODE_MODE_FILE 0x8000
#define EXT4_INODE_MODE_SOFTLINK 0xA000
#define EXT4_INODE_MODE_SOCKET 0xC000
#define EXT4_INODE_MODE_TYPE_MASK 0xF000

/*
 * Inode flags
 */
#define EXT4_INODE_FLAG_SECRM 0x00000001     /* Secure deletion */
#define EXT4_INODE_FLAG_UNRM 0x00000002      /* Undelete */
#define EXT4_INODE_FLAG_COMPR 0x00000004     /* Compress file */
#define EXT4_INODE_FLAG_SYNC 0x00000008      /* Synchronous updates */
#define EXT4_INODE_FLAG_IMMUTABLE 0x00000010 /* Immutable file */
#define EXT4_INODE_FLAG_APPEND 0x00000020  /* writes to file may only append */
#define EXT4_INODE_FLAG_NODUMP 0x00000040  /* do not dump file */
#define EXT4_INODE_FLAG_NOATIME 0x00000080 /* do not update atime */

/* Compression flags */
#define EXT4_INODE_FLAG_DIRTY 0x00000100
#define EXT4_INODE_FLAG_COMPRBLK                                               \
	0x00000200			   /* One or more compressed clusters */
#define EXT4_INODE_FLAG_NOCOMPR 0x00000400 /* Don't compress */
#define EXT4_INODE_FLAG_ECOMPR 0x00000800  /* Compression error */

#define EXT4_INODE_FLAG_INDEX 0x00001000  /* hash-indexed directory */
#define EXT4_INODE_FLAG_IMAGIC 0x00002000 /* AFS directory */
#define EXT4_INODE_FLAG_JOURNAL_DATA                                           \
	0x00004000			  /* File data should be journaled */
#define EXT4_INODE_FLAG_NOTAIL 0x00008000 /* File tail should not be merged */
#define EXT4_INODE_FLAG_DIRSYNC                                                \
	0x00010000 /* Dirsync behaviour (directories only) */
#define EXT4_INODE_FLAG_TOPDIR 0x00020000    /* Top of directory hierarchies */
#define EXT4_INODE_FLAG_HUGE_FILE 0x00040000 /* Set to each huge file */
#define EXT4_INODE_FLAG_EXTENTS 0x00080000   /* Inode uses extents */
#define EXT4_INODE_FLAG_EA_INODE 0x00200000  /* Inode used for large EA */
#define EXT4_INODE_FLAG_EOFBLOCKS 0x00400000 /* Blocks allocated beyond EOF */
#define EXT4_INODE_FLAG_RESERVED 0x80000000  /* reserved for ext4 lib */

#define EXT4_INODE_ROOT_INDEX 2


#define EXT4_DIRECTORY_FILENAME_LEN 255

/**@brief   Directory entry types. */
enum { EXT4_DE_UNKNOWN = 0,
       EXT4_DE_REG_FILE,
       EXT4_DE_DIR,
       EXT4_DE_CHRDEV,
       EXT4_DE_BLKDEV,
       EXT4_DE_FIFO,
       EXT4_DE_SOCK,
       EXT4_DE_SYMLINK };

#define EXT4_DIRENTRY_DIR_CSUM 0xDE

#pragma pack on

union ext4_dir_en_internal {
	u8int name_length_high; /* Higher 8 bits of name length */
	u8int inode_type;       /* Type of referenced inode (in rev >= 0.5) */
};

/**
 * Linked list directory entry structure
 */
struct ext4_dir_en {
	u32int inode;	/* I-node for the entry */
	u16int entry_len; /* Distance to the next directory entry */
	u8int name_len;   /* Lower 8 bits of name length */

	union ext4_dir_en_internal in;
	u8int name[]; /* Entry name */
};

/* Structures for indexed directory */

struct ext4_dir_idx_climit {
	u16int limit;
	u16int count;
};

struct ext4_dir_idx_dot_en {
	u32int inode;
	u16int entry_length;
	u8int name_length;
	u8int inode_type;
	u8int name[4];
};

struct ext4_dir_idx_rinfo {
	u32int reserved_zero;
	u8int hash_version;
	u8int info_length;
	u8int indirect_levels;
	u8int unused_flags;
};

struct ext4_dir_idx_entry {
	u32int hash;
	u32int block;
};

struct ext4_dir_idx_root {
	struct ext4_dir_idx_dot_en dots[2];
	struct ext4_dir_idx_rinfo info;
	struct ext4_dir_idx_entry en[];
};

struct ext4_fake_dir_entry {
	u32int inode;
	u16int entry_length;
	u8int name_length;
	u8int inode_type;
};

struct ext4_dir_idx_node {
	struct ext4_fake_dir_entry fake;
	struct ext4_dir_idx_entry entries[];
};

/*
 * This goes at the end of each htree block.
 */
struct ext4_dir_idx_tail {
	u32int reserved;
	u32int checksum;	/* crc32c(uuid+inum+dirblock) */
};

/*
 * This is a bogus directory entry at the end of each leaf block that
 * records checksums.
 */
struct ext4_dir_entry_tail {
	u32int reserved_zero1;	/* Pretend to be unused */
	u16int rec_len;		/* 12 */
	u8int reserved_zero2;	/* Zero name length */
	u8int reserved_ft;	/* 0xDE, fake file type */
	u32int checksum;		/* crc32c(uuid+inum+dirblock) */
};

#pragma pack off

#define EXT4_DIRENT_TAIL(block, blocksize) \
	((struct ext4_dir_entry_tail *)(((char *)(block)) + ((blocksize) - \
					sizeof(struct ext4_dir_entry_tail))))

#define EXT4_ERR_BAD_DX_DIR (-25000)
#define EXT4_ERR_NOT_FOUND (-25001)

#define EXT4_LINK_MAX 65000

#define EXT4_BAD_INO 1
#define EXT4_ROOT_INO 2
#define EXT4_BOOT_LOADER_INO 5
#define EXT4_UNDEL_DIR_INO 6
#define EXT4_RESIZE_INO 7
#define EXT4_JOURNAL_INO 8

#define EXT4_GOOD_OLD_FIRST_INO 11

#pragma pack on

/*
 * This is the extent tail on-disk structure.
 * All other extent structures are 12 bytes long.  It turns out that
 * block size % 12 >= 4 for at least all powers of 2 greater than 512, which
 * covers all valid ext4 block sizes.  Therefore, this tail structure can be
 * crammed into the end of the block without having to rebalance the tree.
 */
struct ext4_extent_tail
{
	u32int checksum;	/* crc32c(uuid+inum+extent_block) */
};

/*
 * This is the extent on-disk structure.
 * It's used at the bottom of the tree.
 */
struct ext4_extent {
	u32int iblock;	/* First logical block extent covers */
	u16int nblocks;	/* Number of blocks covered by extent */
	u16int fblock_hi;	/* High 16 bits of physical block */
	u32int fblock_lo;	/* Low 32 bits of physical block */
};

/*
 * This is index on-disk structure.
 * It's used at all the levels except the bottom.
 */
struct ext4_extent_index {
	u32int iblock; /* Index covers logical blocks from 'block' */

	/**
	 * Pointer to the physical block of the next
	 * level. leaf or next index could be there
	 * high 16 bits of physical block
	 */
	u32int fblock_lo;
	u16int fblock_hi;
	u16int padding;
};

/*
 * Each block (leaves and indexes), even inode-stored has header.
 */
struct ext4_extent_header {
	u16int magic;
	u16int nentries;	/* Number of valid entries */
	u16int max_nentries;	/* Capacity of store in entries */
	u16int depth;		/* Has tree real underlying blocks? */
	u32int generation;	/* generation of the tree */
};

#pragma pack off

#define EXT4_EXTENT_MAGIC 0xF30A

/******************************************************************************/

/* EXT3 HTree directory indexing */
#define EXT2_HTREE_LEGACY 0
#define EXT2_HTREE_HALF_MD4 1
#define EXT2_HTREE_TEA 2
#define EXT2_HTREE_LEGACY_UNSIGNED 3
#define EXT2_HTREE_HALF_MD4_UNSIGNED 4
#define EXT2_HTREE_TEA_UNSIGNED 5

#define EXT2_HTREE_EOF 0x7FFFFFFFUL

#define EXT4_GOOD_OLD_INODE_SIZE	128

/*****************************************************************************/

/*
 * JBD stores integers in big endian.
 */

#define JBD_MAGIC_NUMBER 0xc03b3998U /* The first 4 bytes of /dev/random! */

/*
 * Descriptor block types:
 */

#define JBD_DESCRIPTOR_BLOCK	1
#define JBD_COMMIT_BLOCK	2
#define JBD_SUPERBLOCK		3
#define JBD_SUPERBLOCK_V2	4
#define JBD_REVOKE_BLOCK	5

#pragma pack on

/*
 * Standard header for all descriptor blocks:
 */
struct jbd_bhdr {
	u32int		magic;
	u32int		blocktype;
	u32int		sequence;
};

#pragma pack off

/*
 * Checksum types.
 */
#define JBD_CRC32_CHKSUM   1
#define JBD_MD5_CHKSUM     2
#define JBD_SHA1_CHKSUM    3
#define JBD_CRC32C_CHKSUM  4

#define JBD_CRC32_CHKSUM_SIZE 4

#define JBD_CHECKSUM_BYTES (32 / sizeof(u32int))

#pragma pack on

/*
 * Commit block header for storing transactional checksums:
 *
 * NOTE: If FEATURE_COMPAT_CHECKSUM (checksum v1) is set, the h_chksum*
 * fields are used to store a checksum of the descriptor and data blocks.
 *
 * If FEATURE_INCOMPAT_CSUM_V2 (checksum v2) is set, then the h_chksum
 * field is used to store crc32c(uuid+commit_block).  Each journal metadata
 * block gets its own checksum, and data block checksums are stored in
 * journal_block_tag (in the descriptor).  The other h_chksum* fields are
 * not used.
 *
 * If FEATURE_INCOMPAT_CSUM_V3 is set, the descriptor block uses
 * journal_block_tag3_t to store a full 32-bit checksum.  Everything else
 * is the same as v2.
 *
 * Checksum v1, v2, and v3 are mutually exclusive features.
 */

struct jbd_commit_header {
	struct jbd_bhdr header;
	u8int chksum_type;
	u8int chksum_size;
	u8int padding[2];
	u32int		chksum[JBD_CHECKSUM_BYTES];
	u64int		commit_sec;
	u32int		commit_nsec;
};

/*
 * The block tag: used to describe a single buffer in the journal
 */
struct jbd_block_tag3 {
	u32int		blocknr;	/* The on-disk block number */
	u32int		flags;	/* See below */
	u32int		blocknr_high; /* most-significant high 32bits. */
	u32int		checksum;	/* crc32c(uuid+seq+block) */
};

struct jbd_block_tag {
	u32int		blocknr;	/* The on-disk block number */
	u16int		checksum;	/* truncated crc32c(uuid+seq+block) */
	u16int		flags;	/* See below */
	u32int		blocknr_high; /* most-significant high 32bits. */
};

#pragma pack off

/* Definitions for the journal tag flags word: */
#define JBD_FLAG_ESCAPE		1	/* on-disk block is escaped */
#define JBD_FLAG_SAME_UUID	2	/* block has same uuid as previous */
#define JBD_FLAG_DELETED	4	/* block deleted by this transaction */
#define JBD_FLAG_LAST_TAG	8	/* last tag in this descriptor block */

#pragma pack on

/* Tail of descriptor block, for checksumming */
struct jbd_block_tail {
	u32int	checksum;
};

/*
 * The revoke descriptor: used on disk to describe a series of blocks to
 * be revoked from the log
 */
struct jbd_revoke_header {
	struct jbd_bhdr  header;
	u32int	 count;	/* Count of bytes used in the block */
};

/* Tail of revoke block, for checksumming */
struct jbd_revoke_tail {
	u32int		checksum;
};

#pragma pack off

#define JBD_USERS_MAX 48
#define JBD_USERS_SIZE (UUID_SIZE * JBD_USERS_MAX)

#pragma pack on

/*
 * The journal superblock.  All fields are in big-endian byte order.
 */
struct jbd_sb {
/* 0x0000 */
	struct jbd_bhdr header;

/* 0x000C */
	/* Static information describing the journal */
	u32int	blocksize;		/* journal device blocksize */
	u32int	maxlen;		/* total blocks in journal file */
	u32int	first;		/* first block of log information */

/* 0x0018 */
	/* Dynamic information describing the current state of the log */
	u32int	sequence;		/* first commit ID expected in log */
	u32int	start;		/* blocknr of start of log */

/* 0x0020 */
	/* Error value, as set by journal_abort(). */
	s32int 	error_val;

/* 0x0024 */
	/* Remaining fields are only valid in a version-2 superblock */
	u32int	feature_compat; 	/* compatible feature set */
	u32int	feature_incompat; 	/* incompatible feature set */
	u32int	feature_ro_compat; 	/* readonly-compatible feature set */
/* 0x0030 */
	u8int 	uuid[UUID_SIZE];		/* 128-bit uuid for journal */

/* 0x0040 */
	u32int	nr_users;		/* Nr of filesystems sharing log */

	u32int	dynsuper;		/* Blocknr of dynamic superblock copy*/

/* 0x0048 */
	u32int	max_transaction;	/* Limit of journal blocks per trans.*/
	u32int	max_trandata;	/* Limit of data blocks per trans. */

/* 0x0050 */
	u8int 	checksum_type;	/* checksum type */
	u8int 	padding2[3];
	u32int	padding[42];
	u32int	checksum;		/* crc32c(superblock) */

/* 0x0100 */
	u8int 	users[JBD_USERS_SIZE];		/* ids of all fs'es sharing the log */

/* 0x0400 */
};

#pragma pack off

#define JBD_SUPERBLOCK_SIZE sizeof(struct jbd_sb)

#define JBD_HAS_COMPAT_FEATURE(jsb,mask)					\
	((jsb)->header.blocktype >= to_be32(2) &&				\
	 ((jsb)->feature_compat & to_be32((mask))))
#define JBD_HAS_RO_COMPAT_FEATURE(jsb,mask)				\
	((jsb)->header.blocktype >= to_be32(2) &&				\
	 ((jsb)->feature_ro_compat & to_be32((mask))))
#define JBD_HAS_INCOMPAT_FEATURE(jsb,mask)				\
	((jsb)->header.blocktype >= to_be32(2) &&				\
	 ((jsb)->feature_incompat & to_be32((mask))))

#define JBD_FEATURE_COMPAT_CHECKSUM	0x00000001

#define JBD_FEATURE_INCOMPAT_REVOKE		0x00000001
#define JBD_FEATURE_INCOMPAT_64BIT		0x00000002
#define JBD_FEATURE_INCOMPAT_ASYNC_COMMIT	0x00000004
#define JBD_FEATURE_INCOMPAT_CSUM_V2		0x00000008
#define JBD_FEATURE_INCOMPAT_CSUM_V3		0x00000010

/* Features known to this kernel version: */
#define JBD_KNOWN_COMPAT_FEATURES	0
#define JBD_KNOWN_ROCOMPAT_FEATURES	0
#define JBD_KNOWN_INCOMPAT_FEATURES	(JBD_FEATURE_INCOMPAT_REVOKE|\
					 JBD_FEATURE_INCOMPAT_ASYNC_COMMIT|\
					 JBD_FEATURE_INCOMPAT_64BIT|\
					 JBD_FEATURE_INCOMPAT_CSUM_V2|\
					 JBD_FEATURE_INCOMPAT_CSUM_V3)

/*****************************************************************************/

#define EXT4_CRC32_INIT (0xFFFFFFFFUL)

/*****************************************************************************/

#define ext4_malloc  malloc
#define ext4_calloc  calloc
#define ext4_realloc realloc
#define ext4_free    free
