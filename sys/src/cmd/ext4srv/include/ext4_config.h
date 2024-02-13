#pragma once

#include <u.h>
#include <libc.h>

typedef enum { false, true } bool;

enum {
	O_RDONLY = 00,
	O_WRONLY = 01,
	O_RDWR = 02,
	O_CREAT = 0100,
	O_EXCL = 0200,
	O_TRUNC = 01000,
	O_APPEND = 02000,

	O_WRMASK = O_WRONLY | O_RDWR,
};

#if defined(__mips__) || defined(__power__) || defined(__power64__) || defined(__sparc__) || defined(__sparc64__)
#define CONFIG_BIG_ENDIAN
#endif

#define CONFIG_EXT4_MAX_BLOCKDEV_NAME 128
#define CONFIG_EXT4_MAX_MP_NAME 128
#define CONFIG_EXT4_BLOCKDEVS_COUNT 32
#define CONFIG_EXT4_MOUNTPOINTS_COUNT 32
#define CONFIG_BLOCK_DEV_CACHE_SIZE 1024

/* Maximum single truncate size. Transactions must be limited to reduce
 * number of allocations for single transaction
 */
#define CONFIG_MAX_TRUNCATE_SIZE (16ul * 1024ul * 1024ul)

extern char Eexists[];
extern char Einval[];
extern char Eio[];
extern char Enomem[];
extern char Enospc[];
extern char Enotfound[];
extern char Eperm[];
extern char Erdonlyfs[];
