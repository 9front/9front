#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"io.h"
#include	"../port/error.h"
#include	"../port/usb.h"
#include	"../port/usbxhci.h"

static void
clkenable(int i, int on)
{
	char clk[32];

	snprint(clk, sizeof(clk), "usb%d.ctrl", i+1);
	setclkgate(clk, on);
	snprint(clk, sizeof(clk), "usb%d.phy", i+1);
	setclkgate(clk, on);
}

static void
phyinit(u32int *reg)
{
	enum {
		PHY_CTRL0 = 0x0/4,
			CTRL0_REF_SSP_EN	= 1<<2,
		PHY_CTRL1 = 0x4/4,
			CTRL1_RESET		= 1<<0,
			CTRL1_ATERESET		= 1<<3,
			CTRL1_VDATSRCENB0	= 1<<19,
			CTRL1_VDATDETEBB0	= 1<<20,
		PHY_CTRL2 = 0x8/4,
			CTRL2_TXENABLEN0	= 1<<8,
	};
	reg[PHY_CTRL1] = (reg[PHY_CTRL1] & ~(CTRL1_VDATSRCENB0 | CTRL1_VDATDETEBB0)) | CTRL1_RESET | CTRL1_ATERESET;
	reg[PHY_CTRL0] |= CTRL0_REF_SSP_EN;
	reg[PHY_CTRL2] |= CTRL2_TXENABLEN0;
	reg[PHY_CTRL1] &= ~(CTRL1_RESET | CTRL1_ATERESET);	
}

static void
coreinit(u32int *reg)
{
	enum {
		GCTL	= 0xC110/4,
			PWRDNSCALE_SHIFT = 19,
			PWRDNSCALE_MASK = 0x3FFF << PWRDNSCALE_SHIFT,
			PRTCAPDIR_SHIFT = 12,
			PRTCAPDIR_MASK = 3 << PRTCAPDIR_SHIFT,
			DISSCRAMBLE = 1<<3,
			DSBLCLKGTNG = 1<<0,

		GUCTL	= 0xC12C/4,
			USBHSTINAUTORETRY = 1<<14,

		GFLADJ	= 0xC630/4,
			GFLADJ_30MHZ_SDBND_SEL = 1<<7,
			GFLADJ_30MHZ_SHIFT = 0,
			GFLADJ_30MHZ_MASK = 0x3F << GFLADJ_30MHZ_SHIFT,

	};
	reg[GCTL] &= ~(PWRDNSCALE_MASK | DISSCRAMBLE | DSBLCLKGTNG | PRTCAPDIR_MASK);
	reg[GCTL] |= 2<<PWRDNSCALE_SHIFT | 1<<PRTCAPDIR_SHIFT;
	reg[GUCTL] |= USBHSTINAUTORETRY;
	reg[GFLADJ] = (reg[GFLADJ] & ~GFLADJ_30MHZ_MASK) | 0x20<<GFLADJ_30MHZ_SHIFT | GFLADJ_30MHZ_SDBND_SEL;
}

static int
reset(Hci *hp)
{
	static char *powerdom[] = { "usb_otg1", "usb_otg2" };
	static Xhci *ctlrs[2];
	Xhci *ctlr;
	int i;

	for(i=0; i<nelem(ctlrs); i++){
		if(ctlrs[i] == nil){
			uintptr base = VIRTIO + 0x8100000 + i*0x100000;
			ctlr = xhcialloc((u32int*)base, base - KZERO, 0x100000);
			if(ctlr == nil)
				break;
			ctlrs[i] = ctlr;
			goto Found;
		}
	}
	return -1;

Found:
	hp->tbdf = BUSUNKNOWN;
	hp->irq = IRQusb1 + i;
	xhcilinkage(hp, ctlr);

	if(i == 0){
		iomuxpad("pad_gpio1_io13", "usb1_otg_oc", "~LVTTL ~HYS ~PUE ~ODE FAST 45_OHM");
		iomuxpad("pad_gpio1_io14", "gpio1_io14", "~LVTTL HYS PUE ~ODE FAST 45_OHM");

		/* gpio1_io14: hub reset */
		gpioout(GPIO_PIN(1, 14), 0);
		microdelay(500);
		gpioout(GPIO_PIN(1, 14), 1);

		for(i = 0; i < nelem(ctlrs); i++) clkenable(i, 0);
		setclkrate("ccm_usb_bus_clk_root", "system_pll2_div2", 500*Mhz);
		setclkrate("ccm_usb_core_ref_clk_root", "system_pll1_div8", 100*Mhz);
		setclkrate("ccm_usb_phy_ref_clk_root", "system_pll1_div8", 100*Mhz);
		i = 0;
	}
	powerup(powerdom[i]);
	clkenable(i, 1);
	phyinit(&ctlr->mmio[0xF0040/4]);
	coreinit(ctlr->mmio);

	return 0;
}

void
usbxhciimxlink(void)
{
	addhcitype("xhci", reset);
}
