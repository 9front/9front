#pragma once

#include "ext4_config.h"
#include "ext4_blockdev.h"

/**@brief Master boot record block devices descriptor*/
struct ext4_mbr_bdevs {
	struct ext4_blockdev partitions[4];
};

int ext4_mbr_scan(struct ext4_blockdev *parent, struct ext4_mbr_bdevs *bdevs);

/**@brief Master boot record partitions*/
struct ext4_mbr_parts {

	/**@brief Percentage division tab:
	 *  - {50, 20, 10, 20}
	 * Sum of all 4 elements must be <= 100*/
	u8int division[4];
};

int ext4_mbr_write(struct ext4_blockdev *parent, struct ext4_mbr_parts *parts, u32int disk_id);
