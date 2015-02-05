#include <u.h>
#include <libc.h>
#include <bio.h>

#include "pci.h"
#include "vga.h"

#include "geode_modes.h"

enum {
	Nregs = 28,
	DC_UNLOCK = 0,
	DC_GENERAL_CFG,
	DC_DISPLAY_CFG,
	DC_ARB_CFG,
	DC_H_ACTIVE_TIMING = 16,
	DC_H_BLANK_TIMING,
	DC_H_SYNC_TIMING,
	DC_V_ACTIVE_TIMING = 20,
	DC_V_BLANK_TIMING,
	DC_V_SYNC_TIMING,
	DC_FB_ACTIVE,
	DC_LINE_SIZE = 12,
	DC_GFX_PITCH,
	
	DC_UNLOCK_VALUE = 0x4758,
	
	/*  DC_GENERAL_CFG */
	VGAE = 1<<7,					/* VGA enable */
	DFLE = 1,						/* display FIFO enable */
	
	/* DC_DISPLAY_CFG */
	TGEN = 1,						/* timing enable */
	GDEN = 1<<3,					/* graphics enable */
	VDEN = 1<<4,					/* video enable */
	TRUP = 1<<6,					/* timing register update */
	PALB = 1<<25,					/* palette bypass */
	DISP_MODE8 = 0,
	DISP_MODE16 = 1<<8,
	DISP_MODE24 = 1<<9,
	DISP_MODE32 = (1<<8) | (1<<9),

	/* low bandwidth */
	LBW_GENERAL = 0x9500,
	LBW_DISPLAY = 0x8000,
	LBW_ARB = 0x150001,
	/* average bandwidth */
	ABW_GENERAL = 0xB600,
	ABW_DISPLAY = 0x9000,
	ABW_ARB = 0x160001,
};

typedef struct Geode Geode;
struct Geode {
	ulong *mmio;
	Pcidev *pci;
	ulong regs[Nregs];
	uvlong clock;
};

static void
snarf(Vga* vga, Ctlr* ctlr)
{
	Geode *geode;
	int i;

	if(!vga->private) {
		geode = alloc(sizeof(Geode));
		geode->pci = vga->pci;
		if(geode->pci == nil){
			geode->pci = pcimatch(0, 0x1022, 0x2081);
			if(!geode->pci) error("%s: not found\n", ctlr->name);
		}
		vgactlpci(geode->pci);
		vgactlw("type", "geode");
		geode->mmio = segattach(0, "geodemmio", 0, geode->pci->mem[2].size);
		if(geode->mmio == (ulong*)-1) error("%s: can't attach mmio segment\n", ctlr->name);
		vga->private = geode;
	}
	else geode = vga->private;
	
	for(i=0;i<Nregs;i++) geode->regs[i] = geode->mmio[i];
	geode->clock = rdmsr(0x4C000015);

	vga->crt[43] = vgaxi(Crtx, 43);
	vga->crt[44] = vgaxi(Crtx, 44);
	vga->crt[47] = vgaxi(Crtx, 47);
	vga->crt[48] = vgaxi(Crtx, 48);
	ctlr->flag |= Fsnarf;
}

static void
options(Vga* vga, Ctlr* ctlr)
{
	USED(vga);
	ctlr->flag |= Foptions;
}

static void
init(Vga* vga, Ctlr* ctlr)
{
	Geode *geode;
	Mode *m;
	int i, bpp;
	
	geode = vga->private;
	m = vga->mode;
	
	/* there has to be a better solution */
	if(m->x < 1024) {
			geode->regs[DC_GENERAL_CFG] = LBW_GENERAL;
			geode->regs[DC_DISPLAY_CFG] = LBW_DISPLAY;
			geode->regs[DC_ARB_CFG] = LBW_ARB;
	} else {
			geode->regs[DC_GENERAL_CFG] = ABW_GENERAL;
			geode->regs[DC_DISPLAY_CFG] = ABW_DISPLAY;
			geode->regs[DC_ARB_CFG] = ABW_ARB;
	}

	geode->regs[DC_GENERAL_CFG] |= DFLE;
	geode->regs[DC_DISPLAY_CFG] |= GDEN | VDEN | TGEN | TRUP | PALB;
	
	switch(m->z) {
		case 8: bpp = 1; break;
		case 15: case 16: bpp = 2; geode->regs[DC_DISPLAY_CFG] |= DISP_MODE16; break;
		case 24: bpp = 3; geode->regs[DC_DISPLAY_CFG] |= DISP_MODE24; break;
		case 32: bpp = 4; geode->regs[DC_DISPLAY_CFG] |= DISP_MODE32; break;
		default: error("%s: unknown bpp value\n", ctlr->name); bpp = 0;
	}
	
	geode->regs[DC_H_ACTIVE_TIMING] = (m->x - 1) | ((m->ht - 1) << 16);
	geode->regs[DC_H_BLANK_TIMING] = (m->shb - 1) | ((m->ehb - 1) << 16);
	geode->regs[DC_H_SYNC_TIMING] = (m->shs - 1) | ((m->ehs - 1) << 16);
	geode->regs[DC_V_ACTIVE_TIMING] = (m->y - 1) | ((m->vt - 1) << 16);
	geode->regs[DC_V_BLANK_TIMING] = (m->vrs - 1) | ((m->vre - 1) << 16);
	geode->regs[DC_V_SYNC_TIMING] = (m->vrs - 1) | ((m->vre - 1) << 16);
	geode->regs[DC_FB_ACTIVE] = (m->x - 1) | ((m->y - 1) << 16);
	geode->regs[DC_GFX_PITCH] = geode->regs[DC_LINE_SIZE] = (m->x >> 3) * bpp;

	for(i=0;i<NumModes;i++)
		if(geode_modes[i][1] == m->frequency)
			goto modefound;
	error("%s: unknown clock value\n", ctlr->name);
modefound:
	geode->clock = ((uvlong)geode_modes[i][0] << 32);
	
	ctlr->flag |= Finit;
}

static void
load(Vga* vga, Ctlr* ctlr)
{
	Geode *geode;
	int i;
	
	geode = vga->private;
	wrmsr(0x4C000015, geode->clock);
	geode->mmio[DC_UNLOCK] = DC_UNLOCK_VALUE;
	for(i=4;i<Nregs;i++) geode->mmio[i] = geode->regs[i];
	for(i=1;i<4;i++) geode->mmio[i] = geode->regs[i];
	ctlr->flag |= Fload;
}

static void
printreg32(ulong u) {
	printreg((u>>24)&0xFF);
	printreg((u>>16)&0xFF);
	printreg((u>>8)&0xFF);
	printreg(u&0xFF);
}

static void
dump(Vga* vga, Ctlr* ctlr)
{
	int i;
	Geode *geode;
	
	geode = vga->private;
	printitem(ctlr->name, "configuration");
	for(i=0;i<4;i++) printreg32(geode->regs[i]);
	printitem(ctlr->name, "memory");
	for(i=4;i<15;i++) printreg32(geode->regs[i]);
	printitem(ctlr->name, "timing");
	for(i=16;i<24;i++) printreg32(geode->regs[i]);
	printitem(ctlr->name, "cursor");
	for(i=24;i<28;i++) printreg32(geode->regs[i]);

	printitem(ctlr->name, "ext");
	printreg(vga->crt[43]);
	printreg(vga->crt[44]);
	printreg(vga->crt[47]);
	printreg(vga->crt[48]);
	
	printitem(ctlr->name, "clock");
	printreg32((geode->clock >> 32) & 0xFFFFFFFF);
	printreg32(geode->clock & 0xFFFFFFFF);
}

Ctlr geode = {
	"geode",				/* name */
	snarf,				/* snarf */
	options,			/* options */
	init,				/* init */
	load,				/* load */
	dump,				/* dump */
};

Ctlr geodehwgc = {
	"geodehwgc",
	0,
	0,
	0,
	0,
	0,
};
