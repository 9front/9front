#include <u.h>
#include "dat.h"
#include "fns.h"

enum {
	QSPI_CFG,
	QSPI_STATUS,
	QSPI_EN = 5,
	QSPI_TXD4 = 7,
	QSPI_RXD,
	QSPI_TXD1 = 32,
	QSPI_TXD2,
	QSPI_TXD3
};

#define QSPI0 ((void *) 0xE000D000)

static u32int
cmd(ulong *r, int sz, u32int c)
{
	if(sz == 4)
		r[QSPI_TXD4] = c;
	else
		r[QSPI_TXD1 + sz - 1] = c;
	r[QSPI_CFG] |= 1<<16;
	while((r[QSPI_STATUS] & (1<<2|1<<4)) != (1<<2|1<<4))
		;
	return r[QSPI_RXD];
}

void
flash(void)
{
	ulong *r;
	
	r = QSPI0;
	r[QSPI_CFG] = 1<<31 | 1<<19 | 3<<6 | 1<<15 | 1<<14 | 1<<10 | 1<<3 | 1;
	r[QSPI_CFG] &= ~(1<<10);
	r[QSPI_EN] = 1;
	cmd(r, 1, 0x06);
//	cmd(r, 3, 0xD8);
	for(;;)
		print("%x\n", cmd(r, 2, 0x05));
	
}
