#include "ext4_config.h"
#include "ext4_types.h"
#include "ext4_misc.h"
#include "ext4_debug.h"
#include "ext4_bitmap.h"

void ext4_bmap_bits_free(u8int *bmap, u32int sbit, u32int bcnt)
{
	u32int i = sbit;

	while(i & 7){
		if(bcnt-- == 0)
			return;
		ext4_bmap_bit_clr(bmap, i++);
	}
	sbit = i;
	bmap += sbit >> 3;

	memset(bmap, 0, bcnt >> 3);
	bmap += bcnt >> 3;
	bcnt &= 7;

	while(bcnt > 0)
		ext4_bmap_bit_clr(bmap, --bcnt);
}

int ext4_bmap_bit_find_clr(u8int *bmap, u32int sbit, u32int ebit,
			   u32int *bit_id, bool *no_space)
{
	u32int i;
	u32int bcnt = ebit - sbit;

	i = sbit;
	*no_space = false;

	while (i & 7) {

		if(!bcnt){
Nospace:
			*no_space = true;
			return -1;
		}

		if (ext4_bmap_is_bit_clr(bmap, i)) {
			*bit_id = sbit;
			return 0;
		}

		i++;
		bcnt--;
	}

	sbit = i;
	bmap += (sbit >> 3);

	while (bcnt >= 8) {
		if (*bmap != 0xFF) {
			for (i = 0; i < 8; ++i) {
				if (ext4_bmap_is_bit_clr(bmap, i)) {
					*bit_id = sbit + i;
					return 0;
				}
			}
		}

		bmap += 1;
		bcnt -= 8;
		sbit += 8;
	}

	for (i = 0; i < bcnt; ++i) {
		if (ext4_bmap_is_bit_clr(bmap, i)) {
			*bit_id = sbit + i;
			return 0;
		}
	}

	goto Nospace;
}
