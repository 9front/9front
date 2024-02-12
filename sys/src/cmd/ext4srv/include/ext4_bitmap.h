#pragma once

#include "ext4_config.h"

/**@brief   Set bitmap bit.
 * @param   bmap bitmap
 * @param   bit bit to set*/
static inline void ext4_bmap_bit_set(u8int *bmap, u32int bit)
{
	*(bmap + (bit >> 3)) |= (1 << (bit & 7));
}

/**@brief   Clear bitmap bit.
 * @param   bmap bitmap buffer
 * @param   bit bit to clear*/
static inline void ext4_bmap_bit_clr(u8int *bmap, u32int bit)
{
	*(bmap + (bit >> 3)) &= ~(1 << (bit & 7));
}

/**@brief   Check if the bitmap bit is set.
 * @param   bmap bitmap buffer
 * @param   bit bit to check*/
static inline bool ext4_bmap_is_bit_set(u8int *bmap, u32int bit)
{
	return (*(bmap + (bit >> 3)) & (1 << (bit & 7)));
}

/**@brief   Check if the bitmap bit is clear.
 * @param   bmap bitmap buffer
 * @param   bit bit to check*/
static inline bool ext4_bmap_is_bit_clr(u8int *bmap, u32int bit)
{
	return !ext4_bmap_is_bit_set(bmap, bit);
}

/**@brief   Free range of bits in bitmap.
 * @param   bmap bitmap buffer
 * @param   sbit start bit
 * @param   bcnt bit count*/
void ext4_bmap_bits_free(u8int *bmap, u32int sbit, u32int bcnt);

/**@brief   Find first clear bit in bitmap.
 * @param   sbit start bit of search
 * @param   ebit end bit of search
 * @param   bit_id output parameter (first free bit)
 * @return  standard error code*/
int ext4_bmap_bit_find_clr(u8int *bmap, u32int sbit, u32int ebit,
			   u32int *bit_id, bool *no_space);
