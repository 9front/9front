/* Based on FreeBSD. */
#pragma once

#include "ext4_config.h"

/**@brief	CRC32 algorithm.
 * @param	crc input feed
 * @param 	buf input buffer
 * @param	size input buffer length (bytes)
 * @return	updated crc32 value*/
u32int ext4_crc32(u32int crc, const void *buf, u32int size);

/**@brief	CRC32C algorithm.
 * @param	crc input feed
 * @param 	buf input buffer
 * @param	length input buffer length (bytes)
 * @return	updated crc32c value*/
u32int ext4_crc32c(u32int crc, const void *buf, u32int size);
