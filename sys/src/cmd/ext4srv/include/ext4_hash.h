#pragma once

#include "ext4_config.h"

struct ext4_hash_info {
	u32int hash;
	u32int minor_hash;
	u32int hash_version;
	const u32int *seed;
};

/**@brief   Directory entry name hash function.
 * @param   name entry name
 * @param   len entry name length
 * @param   hash_seed (from superblock)
 * @param   hash version (from superblock)
 * @param   hash_minor output value
 * @param   hash_major output value
 * @return  standard error code*/
int ext2_htree_hash(const char *name, int len, const u32int *hash_seed,
		    int hash_version, u32int *hash_major,
		    u32int *hash_minor);
