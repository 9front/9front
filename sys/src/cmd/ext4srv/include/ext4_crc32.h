/* Based on FreeBSD. */
#pragma once

#include "ext4_config.h"

/**@brief	CRC32 algorithm.
 * @param	crc input feed
 * @param 	buf input buffer
 * @param	size input buffer length (bytes)
 * @return	updated crc32 value*/
u32int ext4_crc32(u32int crc, const void *buf, u32int size);

extern u32int crc32c_tab[4][256];

/**@brief	CRC32C algorithm.
 * @param	crc input feed
 * @param 	buf input buffer
 * @param	length input buffer length (bytes)
 * @return	updated crc32c value*/
u32int ext4_crc32c(u32int crc, const void *buf, u32int size);

#ifndef CONFIG_BIG_ENDIAN
#define ext4_crc32_u(crc, x) ( \
	(crc) = (crc) ^ (x), \
	crc32c_tab[0][(crc)>>24] ^ \
	crc32c_tab[1][((crc)>>16) & 0xff] ^ \
	crc32c_tab[2][((crc)>>8) & 0xff] ^ \
	crc32c_tab[3][(crc) & 0xff] \
)
#else
#define ext4_crc32_u(crc, x) ( \
	(crc) = (crc) ^ (x), \
	crc32c_tab[0][(crc) & 0xff] ^ \
	crc32c_tab[1][((crc)>>8) & 0xff] ^ \
	crc32c_tab[2][((crc)>>16) & 0xff] ^ \
	crc32c_tab[3][(crc)>>24] \
)
#endif

void ext4_crc32_init(void);
