#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"

/* bcmstb PCIe controller registers */
enum{
	RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1	= 0x0188/4,
	RC_CFG_PRIV1_ID_VAL3			= 0x043c/4,
	RC_DL_MDIO_ADDR				= 0x1100/4,
	RC_DL_MDIO_WR_DATA			= 0x1104/4,
	RC_DL_MDIO_RD_DATA			= 0x1108/4,
	MISC_MISC_CTRL				= 0x4008/4,
	MISC_CPU_2_PCIE_MEM_WIN0_LO		= 0x400c/4,
	MISC_CPU_2_PCIE_MEM_WIN0_HI		= 0x4010/4,
	MISC_RC_BAR1_CONFIG_LO			= 0x402c/4,
	MISC_RC_BAR2_CONFIG_LO			= 0x4034/4,
	MISC_RC_BAR2_CONFIG_HI			= 0x4038/4,
	MISC_RC_BAR3_CONFIG_LO			= 0x403c/4,
	MISC_MSI_BAR_CONFIG_LO			= 0x4044/4,
	MISC_MSI_BAR_CONFIG_HI			= 0x4048/4,
	MISC_MSI_DATA_CONFIG			= 0x404c/4,
	MISC_EOI_CTRL				= 0x4060/4,
	MISC_PCIE_CTRL				= 0x4064/4,
	MISC_PCIE_STATUS			= 0x4068/4,
	MISC_REVISION				= 0x406c/4,
	MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT	= 0x4070/4,
	MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI	= 0x4080/4,
	MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI	= 0x4084/4,
	MISC_HARD_PCIE_HARD_DEBUG		= 0x4204/4,

	INTR2_CPU_BASE				= 0x4300/4,
	MSI_INTR2_BASE				= 0x4500/4,
		INTR_STATUS = 0,
		INTR_SET,
		INTR_CLR,
		INTR_MASK_STATUS,
		INTR_MASK_SET,
		INTR_MASK_CLR,

	EXT_CFG_INDEX				= 0x9000/4,
	RGR1_SW_INIT_1				= 0x9210/4,
	EXT_CFG_DATA				= 0x8000/4,

};

#define MSI_TARGET_ADDR		0xFFFFFFFFCULL

static u32int *regs = (u32int*)(VIRTIO1 + 0x500000);
static Pcidev* pciroot;

static void*
cfgaddr(int tbdf, int rno)
{
	if(BUSBNO(tbdf) == 0 && BUSDNO(tbdf) == 0)
		return (uchar*)regs + rno;
	regs[EXT_CFG_INDEX] = BUSBNO(tbdf) << 20 | BUSDNO(tbdf) << 15 | BUSFNO(tbdf) << 12;
	coherence();
	return ((uchar*)&regs[EXT_CFG_DATA]) + rno;
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

typedef struct Pciisr Pciisr;
struct Pciisr {
	void	(*f)(Ureg*, void*);
	void	*a;
	Pcidev	*p;
};

static Pciisr pciisr[32];
static Lock pciisrlk;

void
pciintrenable(int tbdf, void (*f)(Ureg*, void*), void *a)
{
	ulong dat;
	Pcidev *p;
	Pciisr *isr;

	if((p = pcimatchtbdf(tbdf)) == nil){
		print("pciintrenable: %T: unknown device\n", tbdf);
		return;
	}

	if(pcimsidisable(p) < 0){
		print("pciintrenable: %T: device doesnt support msi\n", tbdf);
		return;
	}

	lock(&pciisrlk);
	for(isr = pciisr; isr < &pciisr[nelem(pciisr)]; isr++){
		if(isr->p == p){
			isr->p = nil;
			regs[MSI_INTR2_BASE + INTR_MASK_SET] = 1 << (isr-pciisr);
			break;
		}
	}
	for(isr = pciisr; isr < &pciisr[nelem(pciisr)]; isr++){
		if(isr->p == nil){
			isr->p = p;
			isr->a = a;
			isr->f = f;
			regs[MSI_INTR2_BASE + INTR_CLR] = 1 << (isr-pciisr);
			regs[MSI_INTR2_BASE + INTR_MASK_CLR] = 1 << (isr-pciisr);
			break;
		}
	}
	unlock(&pciisrlk);

	if(isr >= &pciisr[nelem(pciisr)]){
		print("pciintrenable: %T: out of isr slots\n", tbdf);
		return;
	}

	dat = regs[MISC_MSI_DATA_CONFIG];
	dat = ((dat >> 16) & (dat & 0xFFFF)) | (isr-pciisr);
	pcimsienable(p, MSI_TARGET_ADDR, dat);
}

void
pciintrdisable(int tbdf, void (*f)(Ureg*, void*), void *a)
{
	Pciisr *isr;

	lock(&pciisrlk);
	for(isr = pciisr; isr < &pciisr[nelem(pciisr)]; isr++){
		if(isr->p != nil && isr->p->tbdf == tbdf && isr->f == f && isr->a == a){
			regs[MSI_INTR2_BASE + INTR_MASK_SET] = 1 << (isr-pciisr);
			isr->p = nil;
			isr->f = nil;
			isr->a = nil;
			break;
		}
	}
	unlock(&pciisrlk);
}

static void
pciinterrupt(Ureg *ureg, void*)
{
	Pciisr *isr;
	u32int sts;

	sts = regs[MSI_INTR2_BASE + INTR_STATUS];
	if(sts == 0)
		return;
	regs[MSI_INTR2_BASE + INTR_CLR] = sts;
	for(isr = pciisr; sts != 0 && isr < &pciisr[nelem(pciisr)]; isr++, sts>>=1){
		if((sts & 1) != 0 && isr->f != nil)
			(*isr->f)(ureg, isr->a);
	}
	regs[MISC_EOI_CTRL] = 1;
}

static void
pcicfginit(void)
{
	uvlong base, limit;
	ulong ioa;

	fmtinstall('T', tbdffmt);

	pciscan(0, &pciroot, nil);
	if(pciroot == nil)
		return;

	/*
	 * Work out how big the top bus is
	 */
	ioa = 0;
	base = soc.pciwin;
	pcibusmap(pciroot, &base, &ioa, 0);
	limit = base-1;

	/*
	 * Align the windows and map it
	 */
	base = soc.pciwin;
	regs[MISC_CPU_2_PCIE_MEM_WIN0_LO] = base;
	regs[MISC_CPU_2_PCIE_MEM_WIN0_HI] = base >> 32;
	base >>= 20, limit >>= 20;
	regs[MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT] = (base & 0xFFF) << 4 | (limit & 0xFFF) << 20;
	regs[MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI] = base >> 12;
	regs[MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI] = limit >> 12;

	ioa = 0;
	base = soc.pciwin;
	pcibusmap(pciroot, &base, &ioa, 1);

	pcihinv(pciroot);
}

void
pcibcmlink(void)
{
	int log2dmasize = 30;	// 1GB
	char *s;

	if((s = getconf("*pciwin")) != nil){
		print("*pciwin: %s\n", s);
		soc.pciwin = (uintptr)strtoll(s, nil, 16);
	}
	if((s = getconf("*pcidmawin")) != nil){
		print("*pcidmawin: %s\n", s);
		soc.pcidmawin = (uintptr)strtoll(s, nil, 16);
	}

	regs[RGR1_SW_INIT_1] |= 3;
	delay(200);
	regs[RGR1_SW_INIT_1] &= ~2;
	regs[MISC_PCIE_CTRL] &= ~5;
	delay(200);

	regs[MISC_HARD_PCIE_HARD_DEBUG] &= ~0x08000000;
	delay(200);

	regs[MSI_INTR2_BASE + INTR_CLR] = -1;
	regs[MSI_INTR2_BASE + INTR_MASK_SET] = -1;

	regs[MISC_CPU_2_PCIE_MEM_WIN0_LO] = 0;
	regs[MISC_CPU_2_PCIE_MEM_WIN0_HI] = 0;
	regs[MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT] = 0;
	regs[MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI] = 0;
	regs[MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI] = 0;

	// SCB_ACCESS_EN, CFG_READ_UR_MODE, MAX_BURST_SIZE_128, SCB0SIZE
	regs[MISC_MISC_CTRL] = 1<<12 | 1<<13 | 0<<20 | (log2dmasize-15)<<27;

	regs[MISC_RC_BAR2_CONFIG_LO] = ((u32int)soc.pcidmawin & ~0x1F) | (log2dmasize-15);
	regs[MISC_RC_BAR2_CONFIG_HI] = soc.pcidmawin >> 32;

	regs[MISC_RC_BAR1_CONFIG_LO] = 0;
	regs[MISC_RC_BAR3_CONFIG_LO] = 0;

	regs[MISC_MSI_BAR_CONFIG_LO] = MSI_TARGET_ADDR | 1;
	regs[MISC_MSI_BAR_CONFIG_HI] = MSI_TARGET_ADDR>>32;
	regs[MISC_MSI_DATA_CONFIG] = 0xFFF86540;
	intrenable(IRQpci, pciinterrupt, nil, BUSUNKNOWN, "pci");

	// force to GEN2
	regs[(0xAC + 12)/4] = (regs[(0xAC + 12)/4] & ~15) | 2;	// linkcap
	regs[(0xAC + 48)/4] = (regs[(0xAC + 48)/4] & ~15) | 2;	// linkctl2

	regs[RGR1_SW_INIT_1] &= ~1;
	delay(500);

	if((regs[MISC_PCIE_STATUS] & 0x30) != 0x30){
		print("pcireset: phy link is down\n");
		return;
	}

	regs[RC_CFG_PRIV1_ID_VAL3] = 0x060400;
	regs[RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1] &= ~0xC;
	regs[MISC_HARD_PCIE_HARD_DEBUG] |= 2;

	pcicfginit();
}
