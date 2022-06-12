#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

/* power gating controller registers */
enum {
	GPC_PGC_CPU_0_1_MAPPING	= 0xEC/4,
	GPC_PGC_PU_PGC_SW_PUP_REQ = 0xF8/4,
	GPC_PGC_PU_PGC_SW_PDN_REQ = 0x104/4,
};

static u32int *gpc = (u32int*)(VIRTIO + 0x3A0000);

typedef struct Tab Tab;
struct Tab {
	char	*dom;
	uint	mask;
};

static Tab pu_tab[] = {
	"mipi",		1<<0,
	"pcie",		1<<1,
	"usb_otg1",	1<<2,
	"usb_otg2",	1<<3,
	"ddr1",		1<<5,
	"ddr2",		1<<6,
	"gpu",		1<<7,
	"vpu",		1<<8,
	"hdmi",		1<<9,
	"disp",		1<<10,
	"mipi_csi1",	1<<11,
	"mipi_csi2",	1<<12,
	"pcie2",	1<<13,

	nil,
};

void
powerup(char *dom)
{
	Tab *t;

	if(dom == nil)
		return;

	for(t = pu_tab; t->dom != nil; t++)
		if(cistrcmp(dom, t->dom) == 0)
			goto Found;

	panic("powerup: domain %s not defined", dom);

Found:
	gpc[GPC_PGC_CPU_0_1_MAPPING] = 0x0000FFFF;

	gpc[GPC_PGC_PU_PGC_SW_PUP_REQ] |= t->mask;
	while(gpc[GPC_PGC_PU_PGC_SW_PUP_REQ] & t->mask)
		;

	gpc[GPC_PGC_CPU_0_1_MAPPING] = 0;
}
