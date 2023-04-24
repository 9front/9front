#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"

typedef struct Intvec Intvec;
struct Intvec
{
	Pcidev *p;
	void (*f)(Ureg*, void*);
	void *a;
};

typedef struct Ctlr Ctlr;
struct Ctlr
{
	uvlong	mem_base;
	uvlong	mem_size;
	uvlong	cfg_base;
	uvlong	cfg_size;
	uvlong	io_base;
	uvlong	io_size;

	int	bno, ubn;
	int	irq;

	u32int	*dbi;
	u32int	*cfg;
	Pcidev	*bridge;

	Lock;
	Intvec	vec[32];
};

static Ctlr ctlrs[2] = {
	{
		0x18000000, 0x7f00000,
		0x1ff00000, 0x80000,
		0x1ff80000, 0x10000,
		0, 127, IRQpci1,
		(u32int*)(VIRTIO + 0x3800000),
	},
	{
		0x20000000, 0x7f00000,
		0x27f00000, 0x80000,
		0x27f80000, 0x10000,
		128, 255, IRQpci2,
		(u32int*)(VIRTIO + 0x3c00000),
	},
};

enum {
	IATU_MAX		= 8,
	IATU_INBOUND		= 1<<0,
	IATU_INDEX_SHIFT	= 1,

	IATU_OFFSET		= 0x300000/4,
	IATU_STRIDE		= 0x100/4,

	IATU_REGION_CTRL_1	= 0x00/4,
		CTRL_1_FUNC_NUM_SHIFT		= 20,
		CTRL_1_FUNC_NUM_MASK		= 7<<CTRL_1_FUNC_NUM_SHIFT,

		CTRL_1_INCREASE_REGION_SIZ	= 1<<13,

		CTRL_1_ATTR_SHIFT		= 9,
		CTRL_1_ATTR_MASK		= 3<<CTRL_1_ATTR_SHIFT,

		CTRL_1_TD			= 1<<8,
	
		CTRL_1_TC_SHIFT			= 5,
		CTRL_1_TC_MASK			= 7<<CTRL_1_TC_SHIFT,

		CTRL_1_TYPE_SHIFT		= 0,
		CTRL_1_TYPE_MASK		= 0x1F<<CTRL_1_TYPE_SHIFT,
		CTRL_1_TYPE_MEM			= 0x0<<CTRL_1_TYPE_SHIFT,
		CTRL_1_TYPE_IO			= 0x2<<CTRL_1_TYPE_SHIFT,
		CTRL_1_TYPE_CFG0		= 0x4<<CTRL_1_TYPE_SHIFT,
		CTRL_1_TYPE_CFG1		= 0x5<<CTRL_1_TYPE_SHIFT,

	IATU_REGION_CTRL_2	= 0x04/4,
		CTRL_2_REGION_EN		= 1<<31,
		CTRL_2_INVERT_MODE		= 1<<29,
		CTRL_2_CFG_SHIFT_MODE		= 1<<28,
		CTRL_2_DMA_BYPASS		= 1<<27,
		CTRL_2_HEADER_SUBSITUTE_EN	= 1<<23,
		CTRL_2_INHIBIT_PAYLOAD		= 1<<22,
		CTRL_2_SNP			= 1<<20,
		CTRL_2_FUNC_BYPASS		= 1<<19,
		CTRL_2_TAG_SUBSTITUTE_EN 	= 1<<16,

	IATU_LWR_BSAE_ADDR	= 0x08/4,
	IATU_UPPER_BASE_ADDR	= 0x0C/4,
	IATU_LWR_LIMIT_ADDR	= 0x10/4,
	IATU_LWR_TARGET_ADDR	= 0x14/4,
	IATU_UPPER_TARGET_ADDR	= 0x18/4,
	IATU_UPPER_LIMIT_ADDR	= 0x20/4,	/* undocumented */
};

/* disable all iATU's */
static void
iatuinit(Ctlr *ctlr)
{
	u32int *reg;
	int index;

	for(index=0; index < IATU_MAX; index++){
		reg = &ctlr->dbi[IATU_OFFSET + IATU_STRIDE*index];
		reg[IATU_REGION_CTRL_2] &= ~CTRL_2_REGION_EN;
	}
}

static void
iatucfg(Ctlr *ctlr, int index, u32int type, uvlong target, uvlong base, uvlong size)
{
	uvlong limit = base + size - 1;
	u32int *reg;

	assert(size > 0);
	assert(index < IATU_MAX);
	assert((index & IATU_INBOUND) == 0);

	reg = &ctlr->dbi[IATU_OFFSET + IATU_STRIDE*index];
	reg[IATU_REGION_CTRL_2] &= ~CTRL_2_REGION_EN;

	reg[IATU_LWR_BSAE_ADDR] = base;
	reg[IATU_UPPER_BASE_ADDR] = base >> 32;
	reg[IATU_LWR_LIMIT_ADDR] = limit;
	reg[IATU_UPPER_LIMIT_ADDR] = limit >> 32;
	reg[IATU_LWR_TARGET_ADDR] = target;
	reg[IATU_UPPER_TARGET_ADDR] = target >> 32;

	type &= CTRL_1_TYPE_MASK;
	if(((size-1)>>32) != 0)
		type |= CTRL_1_INCREASE_REGION_SIZ;

	reg[IATU_REGION_CTRL_1] = type;
	reg[IATU_REGION_CTRL_2] = CTRL_2_REGION_EN;

	while((reg[IATU_REGION_CTRL_2] & CTRL_2_REGION_EN) == 0)
		microdelay(10);
}

static Ctlr*
bus2ctlr(int bno)
{
	Ctlr *ctlr;

	for(ctlr = ctlrs; ctlr < &ctlrs[nelem(ctlrs)]; ctlr++)
		if(bno >= ctlr->bno && bno <= ctlr->ubn)
			return ctlr;
	return nil;
}

static void*
cfgaddr(int tbdf, int rno)
{
	Ctlr *ctlr;

	ctlr = bus2ctlr(BUSBNO(tbdf));
	if(ctlr == nil)
		return nil;

	if(pciparentdev == nil){
		if(BUSDNO(tbdf) != 0 || BUSFNO(tbdf) != 0)
			return nil;
		return (uchar*)ctlr->dbi + rno;
	}

	iatucfg(ctlr, 0<<IATU_INDEX_SHIFT,
		pciparentdev->parent==nil? CTRL_1_TYPE_CFG0: CTRL_1_TYPE_CFG1,
		BUSBNO(tbdf)<<24 | BUSDNO(tbdf)<<19 | BUSFNO(tbdf)<<16,
		ctlr->cfg_base, ctlr->cfg_size);

	return (uchar*)ctlr->cfg + rno;
}

int
pcicfgrw32(int tbdf, int rno, int data, int read)
{
	u32int *p;

	if((p = cfgaddr(tbdf, rno & ~3)) != nil){
		if(read)
			data = *p;
		else
			*p = data;
	} else {
		data = -1;
	}
	return data;
}

int
pcicfgrw16(int tbdf, int rno, int data, int read)
{
	u16int *p;

	if((p = cfgaddr(tbdf, rno & ~1)) != nil){
		if(read)
			data = *p;
		else
			*p = data;
	} else {
		data = -1;
	}
	return data;
}

int
pcicfgrw8(int tbdf, int rno, int data, int read)
{
	u8int *p;

	if((p = cfgaddr(tbdf, rno)) != nil){
		if(read)
			data = *p;
		else
			*p = data;
	} else {
		data = -1;
	}
	return data;
}

static u16int msimsg;
#define MSI_TARGET_ADDR		PCIWADDR(&msimsg)

enum {
	MSI_CAP_ID		= 0x50/4,
		PCI_MSI_ENABLE	= 1<<16,

	MSI_CTRL_ADDR		= 0x820/4,
	MSI_CTRL_UPPER_ADDR	= 0x824/4,
	MSI_CTRL_INT_0_EN	= 0x828/4,
	MSI_CTRL_INT_0_MASK	= 0x82C/4,
	MSI_CTRL_INT_0_STATUS	= 0x830/4,

	MISC_CONTROL_1		= 0x8BC/4,
		DBI_RO_WR_EN 	= 1<<0,
};

static void
pciinterrupt(Ureg *ureg, void *arg)
{
	Ctlr *ctlr = arg;
	Intvec *vec;
	u32int status;

	status = ctlr->dbi[MSI_CTRL_INT_0_STATUS];
	if(status == 0)
		return;
	ctlr->dbi[MSI_CTRL_INT_0_STATUS] = status;

	ilock(ctlr);
	for(vec = ctlr->vec; status != 0 && vec < &ctlr->vec[nelem(ctlr->vec)]; vec++, status >>= 1){
		if((status & 1) != 0 && vec->f != nil)
			(*vec->f)(ureg, vec->a);
	}
	iunlock(ctlr);
}

static void
pciintrinit(Ctlr *ctlr)
{
	ctlr->dbi[MSI_CTRL_INT_0_EN] = 0;
	ctlr->dbi[MSI_CTRL_INT_0_MASK] = 0;
	ctlr->dbi[MSI_CTRL_INT_0_STATUS] = -1;
	ctlr->dbi[MSI_CTRL_ADDR] = MSI_TARGET_ADDR;
	ctlr->dbi[MSI_CTRL_UPPER_ADDR] = MSI_TARGET_ADDR >> 32;

	intrenable(ctlr->irq+0, pciinterrupt, ctlr, BUSUNKNOWN, "pci");
	intrenable(ctlr->irq+1, pciinterrupt, ctlr, BUSUNKNOWN, "pci");
	intrenable(ctlr->irq+2, pciinterrupt, ctlr, BUSUNKNOWN, "pci");
	intrenable(ctlr->irq+3, pciinterrupt, ctlr, BUSUNKNOWN, "pci");

	ctlr->dbi[MSI_CAP_ID] |= PCI_MSI_ENABLE;
}

void
pciintrenable(int tbdf, void (*f)(Ureg*, void*), void *a)
{
	Ctlr *ctlr;
	Intvec *vec;
	Pcidev *p;

	ctlr = bus2ctlr(BUSBNO(tbdf));
	if(ctlr == nil){
		print("pciintrenable: %T: unknown controller\n", tbdf);
		return;
	}

	if((p = pcimatchtbdf(tbdf)) == nil){
		print("pciintrenable: %T: unknown device\n", tbdf);
		return;
	}
	if(pcimsidisable(p) < 0){
		print("pciintrenable: %T: device doesnt support vec\n", tbdf);
		return;
	}

	ilock(ctlr);
	for(vec = ctlr->vec; vec < &ctlr->vec[nelem(ctlr->vec)]; vec++){
		if(vec->p == p){
			ctlr->dbi[MSI_CTRL_INT_0_EN] &= ~(1 << (vec - ctlr->vec));
			vec->p = nil;
			break;
		}
	}
	for(vec = ctlr->vec; vec < &ctlr->vec[nelem(ctlr->vec)]; vec++){
		if(vec->p == nil){
			vec->p = p;
			vec->a = a;
			vec->f = f;
			break;
		}
	}
	iunlock(ctlr);

	if(vec >= &ctlr->vec[nelem(ctlr->vec)]){
		print("pciintrenable: %T: out of isr slots\n", tbdf);
		return;
	}
	ctlr->dbi[MSI_CTRL_INT_0_EN] |= (1 << (vec - ctlr->vec));
	pcimsienable(p, MSI_TARGET_ADDR, vec - ctlr->vec);
}

void
pciintrdisable(int tbdf, void (*f)(Ureg*, void*), void *a)
{
	Ctlr *ctlr;
	Intvec *vec;

	ctlr = bus2ctlr(BUSBNO(tbdf));
	if(ctlr == nil){
		print("pciintrenable: %T: unknown controller\n", tbdf);
		return;
	}

	ilock(ctlr);
	for(vec = ctlr->vec; vec < &ctlr->vec[nelem(ctlr->vec)]; vec++){
		if(vec->p == nil)
			continue;
		if(vec->p->tbdf == tbdf && vec->f == f && vec->a == a){
			ctlr->dbi[MSI_CTRL_INT_0_EN] &= ~(1 << (vec - ctlr->vec));
			vec->f = nil;
			vec->a = nil;
			vec->p = nil;
			break;
		}
	}
	iunlock(ctlr);
}

static void
rootinit(Ctlr *ctlr)
{
	uvlong base;
	ulong ioa;

	iatuinit(ctlr);

	ctlr->cfg = vmap(ctlr->cfg_base, ctlr->cfg_size);
	if(ctlr->cfg == nil)
		return;

	ctlr->dbi[MISC_CONTROL_1] |= DBI_RO_WR_EN;

	/* bus number */
	ctlr->dbi[PciPBN/4] &= ~0xFFFFFF;
	ctlr->dbi[PciPBN/4] |= ctlr->bno | (ctlr->bno+1)<<8 | ctlr->ubn<<16;

	/* command */
	ctlr->dbi[PciPCR/4] &= ~0xFFFF;
	ctlr->dbi[PciPCR/4] |= IOen | MEMen | MASen | SErrEn;

	/* device class/subclass */
	ctlr->dbi[PciRID/4] &= ~0xFFFF0000;
	ctlr->dbi[PciRID/4] |=  0x06040000;

	ctlr->dbi[PciBAR0/4] = 0;
	ctlr->dbi[PciBAR1/4] = 0;

	ctlr->dbi[MISC_CONTROL_1] &= ~DBI_RO_WR_EN;

	ctlr->ubn = pciscan(ctlr->bno, &ctlr->bridge, nil);
	if(ctlr->bridge == nil || ctlr->bridge->bridge == nil)
		return;

	pciintrinit(ctlr);

	iatucfg(ctlr, 1<<IATU_INDEX_SHIFT, CTRL_1_TYPE_IO, ctlr->io_base, ctlr->io_base, ctlr->io_size);
	iatucfg(ctlr, 2<<IATU_INDEX_SHIFT, CTRL_1_TYPE_MEM, ctlr->mem_base, ctlr->mem_base, ctlr->mem_size);

	ioa = ctlr->io_base;
	base = ctlr->mem_base;
	pcibusmap(ctlr->bridge, &base, &ioa, 1);

	pcihinv(ctlr->bridge);
}

static void
pcicfginit(void)
{
	fmtinstall('T', tbdffmt);
	rootinit(&ctlrs[0]);
	rootinit(&ctlrs[1]);
}

/* undocumented magic to avoid interference between lcdif and pcie */
static void
qosmagic(void)
{
	static u32int *qosc = (u32int*)(VIRTIO + 0x7f0000);

	/* unlock */
	qosc[0x0000/4] = 0x0;
	qosc[0x0000/4] = 0x1;
	qosc[0x0060/4] = 0x0;

	/* pci1 */
	qosc[0x1000/4] = 0x0;
	qosc[0x1000/4] = 0x1;
	qosc[0x1050/4] = 0x01010100;
	qosc[0x1060/4] = 0x01010100;
	qosc[0x1070/4] = 0x01010100;
	qosc[0x1000/4] = 0x1;

	/* pcie2 */
	qosc[0x2000/4] = 0x0;
	qosc[0x2000/4] = 0x1;
	qosc[0x2050/4] = 0x01010100;
	qosc[0x2060/4] = 0x01010100;
	qosc[0x2070/4] = 0x01010100;
	qosc[0x2000/4] = 0x1;
}

enum {
	SRC_PCIEPHY_RCR		= 0x2C/4,
	SRC_PCIE2_RCR		= 0x48/4,
		PCIE_CTRL_APP_XFER_PENDING	= 1<<16,
		PCIE_CTRL_APP_UNLOCK_MSG	= 1<<15,
		PCIE_CTRL_SYS_INT		= 1<<14,
		PCIE_CTRL_CFG_L1_AUX		= 1<<12,
		PCIE_CTRL_APPS_TURNOFF		= 1<<11,
		PCIE_CTRL_APPS_PME		= 1<<10,
		PCIE_CTRL_APPS_EXIT		= 1<<9,
		PCIE_CTRL_APPS_ENTER		= 1<<8,
		PCIE_CTRL_APPS_READY		= 1<<7,
		PCIE_CTRL_APPS_EN		= 1<<6,
		PCIE_CTRL_APPS_RST		= 1<<5,
		PCIE_CTRL_APPS_CLK_REQ		= 1<<4,
		PCIE_PERST			= 1<<3,
		PCIE_BTN			= 1<<2,
		PCIE_G_RST			= 1<<1,
		PCIE_PHY_POWER_ON_RESET_N	= 1<<0,
};

static u32int *resetc = (u32int*)(VIRTIO + 0x390000);

void
pciimxlink(void)
{
	resetc[SRC_PCIEPHY_RCR] |= PCIE_BTN | PCIE_G_RST;
	resetc[SRC_PCIE2_RCR] |= PCIE_BTN | PCIE_G_RST;

	resetc[SRC_PCIEPHY_RCR] |= PCIE_CTRL_APPS_EN;
	resetc[SRC_PCIE2_RCR] |= PCIE_CTRL_APPS_EN;

	setclkgate("pcie_clk_rst.auxclk", 0);
	setclkgate("pcie2_clk_rst.auxclk", 0);

	iomuxpad("pad_ecspi1_mosi", "gpio5_io07", "~LVTTL ~HYS ~PUE ~ODE FAST 45_OHM");
	iomuxpad("pad_sai5_rxd2", "gpio3_io23", "~LVTTL ~HYS ~PUE ~ODE FAST 45_OHM");

	gpioout(GPIO_PIN(5, 7), 0);
	gpioout(GPIO_PIN(3, 23), 0);

	powerup("pcie");
	powerup("pcie2");

	/* configure monitor CLK2 output internal reference clock for PCIE1 */
	setclkrate("ccm_analog_pllout", "system_pll1_clk", 100*Mhz);
	delay(10);

	/* PCIE1_REF_USE_PAD=0 */
	iomuxgpr(14, 0<<9, 1<<9);

	/* PCIE2_REF_USE_PAD=1 */
	iomuxgpr(16, 1<<9, 1<<9);

	/* PCIE1_CTRL_DEVICE_TYPE=ROOT, PCIE2_CTRL_DEVICE_TYPE=ROOT */
	iomuxgpr(12, 4<<12 | 4<<8, 0xF<<12 | 0xF<<8);

	setclkrate("ccm_pcie1_ctrl_clk_root", "system_pll2_div4", 250*Mhz);
	setclkrate("ccm_pcie2_ctrl_clk_root", "system_pll2_div4", 250*Mhz);

	setclkrate("pcie_clk_rst.auxclk", "system_pll2_div10", 100*Mhz);
	setclkrate("pcie2_clk_rst.auxclk", "system_pll2_div10", 100*Mhz);

	setclkrate("pcie_phy.ref_alt_clk_p", "system_pll2_div10", 100*Mhz);
	setclkrate("pcie2_phy.ref_alt_clk_p", "system_pll2_div10", 100*Mhz);

	setclkgate("pcie_clk_rst.auxclk", 1);
	setclkgate("pcie2_clk_rst.auxclk", 1);

	/* PCIE1_CLKREQ_B_OVERRIDE=0 PCIE1_CLKREQ_B_OVERRIDE_EN=1 */
	iomuxgpr(14, 1<<10, 3<<10);

	/* PCIE2_CLKREQ_B_OVERRIDE=0 PCIE2_CLKREQ_B_OVERRIDE_EN=1 */
	iomuxgpr(16, 1<<10, 3<<10);

	delay(100);
	gpioout(GPIO_PIN(5, 7), 1);
	gpioout(GPIO_PIN(3, 23), 1);
	delay(1);

	resetc[SRC_PCIEPHY_RCR] &= ~(PCIE_BTN | PCIE_G_RST);
	resetc[SRC_PCIE2_RCR] &= ~(PCIE_BTN | PCIE_G_RST);

	pcicfginit();

	qosmagic();
}
