/*
 * bcm2835 sdhost controller
 */

#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/sd.h"

#define SDHOSTREGS	(VIRTIO+0x202000)

enum {
	HC_COMMAND		= 0x00>>2,
		HC_CMD_ENABLE		= 0x8000,
		HC_CMD_FAILED		= 0x4000,
		HC_CMD_BUSY		= 0x0800,
		HC_CMD_RESPONSE_NONE	= 0x0400,
		HC_CMD_RESPONSE_LONG	= 0x0200,
		HC_CMD_WRITE		= 0x0080,
		HC_CMD_READ		= 0x0040,
		HC_CMD_MASK		= 0x003F,

	HC_ARGUMENT		= 0x04>>2,
	HC_TIMEOUTCOUNTER	= 0x08>>2,

	HC_CLOCKDIVISOR		= 0x0c>>2,
		HC_CLKDIV_MAX		= 0x07FF,

	HC_RESPONSE_0		= 0x10>>2,
	HC_RESPONSE_1		= 0x14>>2,
	HC_RESPONSE_2		= 0x18>>2,
	HC_RESPONSE_3		= 0x1c>>2,

	HC_HOSTSTATUS		= 0x20>>2,
		HC_HSTST_HAVE_DATA	= 0x0001,
		HC_HSTST_ERROR_FIFO	= 0x0008,
		HC_HSTST_ERROR_CRC7	= 0x0010,
		HC_HSTST_ERROR_CRC16	= 0x0020,
		HC_HSTST_TIMEOUT_CMD	= 0x0040,
		HC_HSTST_TIMEOUT_DATA	= 0x0080,
		HC_HSTST_INT_BLOCK	= 0x0200,
		HC_HSTST_INT_BUSY	= 0x0400,
		HC_HSTST_RESET		= 0xFFFF,
		HC_HSTST_ERROR
			= HC_HSTST_ERROR_FIFO
			| HC_HSTST_ERROR_CRC7
			| HC_HSTST_ERROR_CRC16
			| HC_HSTST_TIMEOUT_CMD
			| HC_HSTST_TIMEOUT_DATA,

	HC_POWER		= 0x30>>2,
	HC_DEBUG		= 0x34>>2,

	HC_HOSTCONFIG		= 0x38>>2,
		HC_HSTCF_INTBUS_WIDE	= 0x0002,
		HC_HSTCF_EXTBUS_4BIT	= 0x0004,
		HC_HSTCF_SLOW_CARD	= 0x0008,
		HC_HSTCF_INT_DATA	= 0x0010,
		HC_HSTCF_INT_BLOCK	= 0x0100,
		HC_HSTCF_INT_BUSY	= 0x0400,

	HC_BLOCKSIZE		= 0x3C>>2,
	HC_DATAPORT		= 0x40>>2,
	HC_BLOCKCOUNT		= 0x50>>2,
};

static u32int
RD(int reg)
{
	u32int *r = (u32int*)SDHOSTREGS;
	return r[reg];
}

static void
WR(int reg, u32int val)
{
	u32int *r = (u32int*)SDHOSTREGS;

	if(0)print("WR %2.2ux %ux\n", reg<<2, val);
	coherence();
	r[reg] = val;
}

static void
sdhostclk(uint freq)
{
	uint div, ext;

	ext = getclkrate(ClkCore);
	div = freq / ext;
	if(div < 2)
		div = 2;
	if((ext / div) > freq)
		div++;
	div -= 2;
	if(div > HC_CLKDIV_MAX)
		div = HC_CLKDIV_MAX;
	freq = ext / (div+2);
	WR(HC_CLOCKDIVISOR, div);
	WR(HC_TIMEOUTCOUNTER, freq/2);	/* 500ms timeout */
}

static void
sdhostbus(SDio*, int width, int speed)
{
	switch(width){
	case 1:
		WR(HC_HOSTCONFIG, RD(HC_HOSTCONFIG) & ~HC_HSTCF_EXTBUS_4BIT);
		break;
	case 4:
		WR(HC_HOSTCONFIG, RD(HC_HOSTCONFIG) |  HC_HSTCF_EXTBUS_4BIT);
		break;
	}
	if(speed)
		sdhostclk(speed);
}

static int
sdhostinit(SDio*)
{
	WR(HC_POWER, 0);
	WR(HC_COMMAND, 0);
	WR(HC_ARGUMENT, 0);
	WR(HC_TIMEOUTCOUNTER, 0);
	WR(HC_CLOCKDIVISOR, 0);
	WR(HC_HOSTSTATUS, HC_HSTST_RESET);
	WR(HC_HOSTCONFIG, 0);
	WR(HC_BLOCKSIZE, 0);
	WR(HC_BLOCKCOUNT, 0);
	microdelay(20);
	return 0;
}

static int
sdhostinquiry(SDio*, char *inquiry, int inqlen)
{
	return snprint(inquiry, inqlen, "BCM SD Host Controller");
}

static void
sdhostenable(SDio*)
{
	WR(HC_POWER, 1);
	WR(HC_HOSTCONFIG, HC_HSTCF_INTBUS_WIDE|HC_HSTCF_SLOW_CARD);

	sdhostclk(25*Mhz);
}

static void
sdhosterror(u32int i)
{
	snprint(up->genbuf, sizeof(up->genbuf), "sdhost error %#ux\n", i);
	error(up->genbuf);
}

static int
sdhostcmd(SDio*, SDiocmd *cmd, u32int arg, u32int *resp)
{
	u32int c, i;
	ulong now;

	c = cmd->index & HC_CMD_MASK;
	switch(cmd->resp){
	case 0:
		c |= HC_CMD_RESPONSE_NONE;
		break;
	case 1:
		if(cmd->busy)
			c |= HC_CMD_BUSY;
	default:
		break;
	case 2:
		c |= HC_CMD_RESPONSE_LONG;
		break;
	}
	if(cmd->data){
		if(cmd->data & 1)
			c |= HC_CMD_READ;
		else
			c |= HC_CMD_WRITE;
	}

	/* clear errors */
	WR(HC_HOSTSTATUS, RD(HC_HOSTSTATUS));

	WR(HC_ARGUMENT, arg);
	WR(HC_COMMAND, c | HC_CMD_ENABLE);

	now = MACHP(0)->ticks;
	while((i = RD(HC_COMMAND)) & HC_CMD_ENABLE)
		if(MACHP(0)->ticks - now > HZ)
			break;

	if(i & HC_CMD_ENABLE)
		error("command never completed");
	if(i & HC_CMD_FAILED)
		error("command failed");

	if((i = RD(HC_HOSTSTATUS)) & HC_HSTST_ERROR)
		sdhosterror(i);

	if(c & HC_CMD_RESPONSE_NONE) {
		resp[0] = 0;
	} else if(c & HC_CMD_RESPONSE_LONG) {
		resp[0] = RD(HC_RESPONSE_0);
		resp[1] = RD(HC_RESPONSE_1);
		resp[2] = RD(HC_RESPONSE_2);
		resp[3] = RD(HC_RESPONSE_3);
	} else {
		resp[0] = RD(HC_RESPONSE_0);
	}
	return 0;
}

static void
sdhostiosetup(SDio*, int, void *, int bsize, int bcount)
{
	WR(HC_BLOCKSIZE, bsize);
	WR(HC_BLOCKCOUNT, bcount);
}

static void
sdhostio(SDio*, int write, uchar *buf, int len)
{
	u32int i, *r = (u32int*)SDHOSTREGS;

	if(write)
		dmastart(DmaChanSdhost, DmaDevSdhost, DmaM2D, buf, r + HC_DATAPORT, len);
	else
		dmastart(DmaChanSdhost, DmaDevSdhost, DmaD2M, r + HC_DATAPORT, buf, len);

	if(dmawait(DmaChanSdhost) < 0)
		error(Eio);

	if((i = RD(HC_HOSTSTATUS)) & HC_HSTST_ERROR)
		sdhosterror(i);
}

static void
sdhostled(SDio*, int on)
{
	okay(on);
}

void
sdhostlink(void)
{
	static SDio io = {
		"sdhost",
		sdhostinit,
		sdhostenable,
		sdhostinquiry,
		sdhostcmd,
		sdhostiosetup,
		sdhostio,
		sdhostbus,
		sdhostled,
	};
	addmmcio(&io);
}
