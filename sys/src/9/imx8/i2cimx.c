#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/i2c.h"

enum {
	Moduleclk = 25*Mhz,

	I2C_IADR	= 0x00,
	I2C_IFDR	= 0x04,
	I2C_I2CR	= 0x08,
		I2CR_IEN	= 1<<7,
		I2CR_IIEN	= 1<<6,
		I2CR_MSTA	= 1<<5,
		I2CR_MTX	= 1<<4,
		I2CR_TXAK	= 1<<3,
		I2CR_RSTA	= 1<<2,
	I2C_I2SR	= 0x0C,
		I2SR_ICF	= 1<<7,
		I2SR_IAAS	= 1<<6,
		I2SR_IBB	= 1<<5,
		I2SR_IAL	= 1<<4,
		I2SR_SRW	= 1<<2,
		I2SR_IIF	= 1<<1,
		I2SR_RXAK	= 1<<0,
	I2C_I2DR	= 0x10,
};

typedef struct Ctlr Ctlr;
struct Ctlr
{
	void	*regs;
	int	irq;

	Rendez;
};

static void
interrupt(Ureg*, void *arg)
{
	I2Cbus *bus = arg;
	Ctlr *ctlr = bus->ctlr;
	wakeup(ctlr);
}

static int
haveirq(void *arg)
{
	uchar *regs = arg;
	return regs[I2C_I2SR] & (I2SR_IAL|I2SR_IIF);
}

static int
waitsr(Ctlr *ctlr, int inv, int mask)
{
	uchar *regs = ctlr->regs;
	int sr;

	for(;;){
		sr = regs[I2C_I2SR];
		if(sr & I2SR_IAL){
			regs[I2C_I2SR] = sr & ~(I2SR_IAL|I2SR_IIF);
			break;
		}
		if(sr & I2SR_IIF)
			regs[I2C_I2SR] = sr & ~I2SR_IIF;
		if((sr ^ inv) & mask)
			break;

		/* polling mode */
		if(up == nil || !islo())
			continue;

		tsleep(ctlr, haveirq, regs, 1);
	}
	return sr ^ inv;
}

static uchar dummy;

static int
io(I2Cdev *dev, uchar *pkt, int olen, int ilen)
{
	I2Cbus *bus = dev->bus;
	Ctlr *ctlr = bus->ctlr;
	uchar *regs = ctlr->regs;
	int cr, sr, alen, o, i;

	cr = regs[I2C_I2CR];
	if((cr & I2CR_IEN) == 0)
		return -1;

	o = 0;
	if(olen <= 0)
		goto Stop;

	alen = 1;
	if((pkt[0] & 0xF8) == 0xF0 && olen > alen)
		alen++;

	regs[I2C_IADR] = (pkt[0]&0xFE)^0xFE;	/* make sure doesnt match */

	/* wait for bus idle */
	waitsr(ctlr, I2SR_IBB, I2SR_IBB);

	/* start */
	cr |= I2CR_MSTA | I2CR_MTX | I2CR_TXAK | I2CR_IIEN;
	regs[I2C_I2CR] = cr;

	/* wait for bus busy */
	if(waitsr(ctlr, 0, I2SR_IBB) & I2SR_IAL)
		goto Err;

	if(olen > alen)
		pkt[0] &= ~1;

	for(o=0; o<olen; o++){
		regs[I2C_I2DR] = pkt[o];
		sr = waitsr(ctlr, 0, I2SR_IIF);
		if(sr & I2SR_IAL)
			goto Err;
		if(sr & I2SR_RXAK)
			goto Stop;
	}

	if(ilen <= 0)
		goto Stop;

	if((pkt[0]&1) == 0){
		regs[I2C_I2CR] = cr | I2CR_RSTA;

		pkt[0] |= 1;
		for(i=0; i<alen; i++){
			regs[I2C_I2DR] = pkt[i];
			sr = waitsr(ctlr, 0, I2SR_IIF);
			if(sr & I2SR_IAL)
				goto Err;
			if(sr & I2SR_RXAK)
				goto Stop;
		}
	}

	cr &= ~(I2CR_MTX | I2CR_TXAK);
	if(ilen == 1) cr |= I2CR_TXAK;
	regs[I2C_I2CR] = cr;
	dummy = regs[I2C_I2DR];	/* start the next read */

	for(i=1; i<=ilen; i++){
		sr = waitsr(ctlr, I2SR_ICF, I2SR_IIF);
		if(sr & I2SR_IAL)
			goto Err;
		if(sr & I2SR_ICF)
			goto Stop;
		if(i == ilen){
			cr &= ~(I2CR_MSTA|I2CR_IIEN);
			regs[I2C_I2CR] = cr;
		}
		else if(i == ilen-1){
			cr |= I2CR_TXAK;
			regs[I2C_I2CR] = cr;
		}
		pkt[o++] = regs[I2C_I2DR];
	}

	return o;
Err:
	o = -1;
Stop:
	cr &= ~(I2CR_MTX|I2CR_MSTA|I2CR_RSTA|I2CR_IIEN);
	regs[I2C_I2CR] = cr;
	return o;
}

static int
divindex(int v)
{
	static int tab[] = {
	/* 0x00 */  30,  32,  36,  42,  48,  52,  60,  72,
	/* 0x08 */  80,  88, 104, 128, 144, 160, 192, 240,
	/* 0x10 */ 288, 320, 384, 480, 576, 640, 768, 960,
	/* 0x18 */1152,1280,1536,1920,2304,2560,3072,3840,
	/* 0x20 */  22,  24,  26,  28,  32,  36,  40,  44,
	/* 0x28 */  48,  56,  64,  72,  80,  96, 112, 128,
	/* 0x30 */ 160, 192, 224, 256, 320, 384, 448, 512,
	/* 0x38 */ 640, 768, 896,1024,1280,1536,1792,2048,
	};
	int i, x = -1;
	for(i = 0; i < nelem(tab); i++){
		if(tab[i] < v)
			continue;
		if(x < 0 || tab[i] < tab[x]){
			x = i;
			if(tab[i] == v)
				break;
		}
	}
	return x;
}

static void
clkenable(char *name, int on)
{
	char clk[32];

	snprint(clk, sizeof(clk), "%s.ipg_clk_patref", name);
	setclkgate(clk, 0);
	if(on) {
		setclkrate(clk, "osc_25m_ref_clk", Moduleclk);
		setclkgate(clk, 1);
	}
}

static int
init(I2Cbus *bus)
{
	Ctlr *ctlr = bus->ctlr;
	uchar *regs = ctlr->regs;

	clkenable(bus->name, 1);

	regs[I2C_IFDR] = divindex(Moduleclk / bus->speed);
	regs[I2C_IADR] = 0;
	
	regs[I2C_I2CR] = I2CR_IEN;
	delay(1);

	intrenable(ctlr->irq, interrupt, bus, BUSUNKNOWN, bus->name);

	return 0;
}

static Ctlr ctlr1 = {
	.regs = (void*)(VIRTIO + 0xA20000),
	.irq = IRQi2c1,
};
static Ctlr ctlr2 = {
	.regs = (void*)(VIRTIO + 0xA30000),
	.irq = IRQi2c2,
};
static Ctlr ctlr3 = {
	.regs = (void*)(VIRTIO + 0xA40000),
	.irq = IRQi2c3,
};
static Ctlr ctlr4 = {
	.regs = (void*)(VIRTIO + 0xA50000),
	.irq = IRQi2c4,
};

void
i2cimxlink(void)
{
	static I2Cbus i2c1 = { "i2c1", 400000, &ctlr1, init, io };
	static I2Cbus i2c3 = { "i2c3", 400000, &ctlr3, init, io };
	static I2Cbus i2c4 = { "i2c4", 400000, &ctlr4, init, io };

	iomuxpad("pad_i2c1_sda", "i2c1_sda", "SION ~LVTTL ~HYS PUE ODE MAX 40_OHM");
	iomuxpad("pad_i2c1_scl", "i2c1_scl", "SION ~LVTTL ~HYS PUE ODE MAX 40_OHM");
	addi2cbus(&i2c1);

	iomuxpad("pad_i2c3_sda", "i2c3_sda", "SION ~LVTTL ~HYS PUE ODE MAX 40_OHM");
	iomuxpad("pad_i2c3_scl", "i2c3_scl", "SION ~LVTTL ~HYS PUE ODE MAX 40_OHM VSEL_0");
	addi2cbus(&i2c3);

	iomuxpad("pad_i2c4_sda", "i2c4_sda", "SION ~LVTTL ~HYS PUE ODE MAX 40_OHM");
	iomuxpad("pad_i2c4_scl", "i2c4_scl", "SION ~LVTTL ~HYS PUE ODE MAX 40_OHM");
	addi2cbus(&i2c4);
}
