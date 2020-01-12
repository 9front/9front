#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"

#define dmar ((ulong*)DMAS_BASE)

enum {
	DSR = 0x000,
	DPC = 0x004/4,
	INTEN = 0x20 / 4,
	INT_EVENT_RIS = 0x024 / 4,
	INTMIS = 0x028 / 4,
	INTCLR = 0x02C / 4,
	FSRD = 0x030 / 4,
	FSRC = 0x034 / 4,
	FTRD = 0x038 / 4,
	DBGSTATUS = 0xD00 / 4,
	DBGCMD = 0xD04 / 4,
	DBGINST0 = 0xD08 / 4,
	DBGINST1 = 0xD0C / 4,
	CR0 = 0xE00 / 4,
	CR1, CR2, CR3, CR4, CRD,
	WD = 0xE80 / 4,
};
enum {
	DMAStopped,
	DMAExecuting,
	DMACacheMiss,
	DMAUpdatingPC,
	DMAWaitingForEvent,
	DMAAtBarrier,
	DMAWaitingForPeripheral = 7,
	DMAKilling,
	DMACompleting,
	DMAFaultingCompleting=14,
	DMAFaulting,
};
#define FTR(n) dmar[(0x40/4 + (n))]
#define CSR(n) dmar[(0x100/4 + (n)*2)]
#define CPC(n) dmar[(0x104/4 + (n)*2)]
#define SAR(n) dmar[(0x400/4 + (n)*8)]
#define DAR(n) dmar[(0x404/4 + (n)*8)]
#define CCR(n) dmar[(0x408/4 + (n)*8)]
#define LC0(n) dmar[(0x40C/4 + (n)*8)]
#define LC1(n) dmar[(0x410/4 + (n)*8)]

#define DST_BURST(n) (((n)-1&0xf)<<18)
#define DST_BEAT_1 (0)
#define DST_BEAT_2 (1<<15)
#define DST_BEAT_4 (2<<15)
#define DST_BEAT_8 (3<<15)
#define DST_BEAT_16 (4<<15)
#define SRC_BURST(n) (((n)-1&0xf)<<4)
#define SRC_BEAT_1 (0)
#define SRC_BEAT_2 (1<<1)
#define SRC_BEAT_4 (2<<1)
#define SRC_BEAT_8 (3<<1)
#define SRC_BEAT_16 (4<<1)

#define dmaMOV_SARn 0x00bc
#define dmaMOV_CCRn 0x01bc
#define dmaMOV_DARn 0x02bc
#define dmaLP0(n) (((n)-1&0xff)<<8|0x20)
#define dmaLP1(n) (((n)-1&0xff)<<8|0x22)
#define dmaLPEND0(n) (((n)&0xff)<<8|0x38)
#define dmaLPEND1(n) (((n)&0xff)<<8|0x3c)
#define dmaLD 0x04
#define dmaST 0x08
#define dmaSEV(n) (((n)&31)<<11|0x34)
#define dmaEND 0x00
#define dmaWMB 0x13

static QLock dmalock;
static Rendez dmarend;
static int finished;
static ulong code[64];

static int
isfinished(void *)
{
	return finished;
}
static void
dmairq(Ureg *, void *)
{
	dmar[INTCLR] = -1;
	finished = 1;
	wakeup(&dmarend);
}

static void
compactify(ulong *lp)
{
	uchar *p, *q;
	
	q = p = (uchar *) lp;
	for(;;){
		switch(*p){
		case 0xbc:
			q[0] = p[0];
			q[1] = p[1];
			q[2] = p[4];
			q[3] = p[5];
			q[4] = p[6];
			q[5] = p[7];
			q += 6; p += 8;
			break;
		case 0x20: case 0x22: case 0x38: case 0x3c: case 0x34:
			q[0] = p[0];
			q[1] = p[1];
			q += 2; p += 4;
			break;
		case 0x04: case 0x08: case 0x13:
			q[0] = p[0];
			q++; p += 4;
			break;
		case 0x00:
			q[0] = 0;
			return;
		default:
			panic("DMA: unknown opcode %.2x", *p);
		}
	}
}

#define BURST(n) *p++ = dmaMOV_CCRn, *p++ = DST_BEAT_4 | SRC_BEAT_4 | SRC_BURST(n) | DST_BURST(n) | attr

void
dmacopy(void *dst, void *src, ulong n, int attr)
{
	ulong *p;
	
	assert((n & 3) == 0 && ((uintptr)src & 3) == 0 && ((uintptr)dst & 3) == 0);
	while(n > (1<<22)){
		dmacopy(dst, src, 1<<22, attr);
		if((attr & SRC_INC) != 0) src = (uchar*)src + (1<<22);
		if((attr & DST_INC) != 0) dst = (uchar*)dst + (1<<22);
	}
	if(n == 0) return;
	qlock(&dmalock);
	p = code;
	*p++ = dmaMOV_SARn; *p++ = PADDR(src);
	*p++ = dmaMOV_DARn; *p++ = PADDR(dst);
	if((n >> 6) >= 1){
		BURST(16);
		if((n>>14) >= 1){
			if((n>>14) > 1) *p++ = dmaLP0(n >> 14);
			*p++ = dmaLP1(256);
			*p++ = dmaLD;
			*p++ = dmaST;
			*p++ = dmaLPEND1(2);
			if((n>>14) > 1) *p++ = dmaLPEND0(6);
			n &= (1<<14)-1;
		}
		if((n >> 6) >= 1){
			if((n>>6) > 1) *p++ = dmaLP0(n >> 6);
			*p++ = dmaLD;
			*p++ = dmaST;
			if((n>>6) > 1) *p++ = dmaLPEND0(2);
			n &= 63;
		}
	}
	if(n >= 4){
		BURST(n>>2);
		*p++ = dmaLD;
		*p++ = dmaST;
	}
	*p++ = dmaWMB;
	*p++ = dmaSEV(0);
	*p = dmaEND;
	compactify(code);
	if((CSR(0) & 0xf) != DMAStopped){
		while((dmar[DBGSTATUS] & 1) != 0)
			tsleep(&up->sleep, return0, nil, 1);
		dmar[DBGINST0] = 0x1 << 16 | 1;
		dmar[DBGCMD] = 0;
		while((dmar[DBGSTATUS] & 1) != 0)
			tsleep(&up->sleep, return0, nil, 1);
		while((CSR(0) & 0xf) != DMAStopped)
			tsleep(&up->sleep, return0, nil, 1);
	}
	cleandse(code, code + nelem(code));
	while((dmar[DBGSTATUS] & 1) != 0)
		tsleep(&up->sleep, return0, nil, 1);
	dmar[DBGINST0] = 0xa0 << 16;
	dmar[DBGINST1] = PADDR(code);
	finished = 0;
	dmar[DBGCMD] = 0;
	while(!finished)
		sleep(&dmarend, isfinished, nil);
	qunlock(&dmalock);
}


static void
dmaabort(Ureg *, void *)
{
	int i;

	if((dmar[FSRD] & 1) != 0){
		iprint("dma: manager fault: ");
		if((dmar[FTRD] & 1<<30) != 0)
			iprint("debug instruction, ");
		if((dmar[FTRD] & 1<<16) != 0)
			iprint("instruction fetch error, ");
		if((dmar[FTRD] & 1<<5) != 0)
			iprint("event security violation, ");
		if((dmar[FTRD] & 1<<4) != 0)
			iprint("DMAGO security violation, ");
		if((dmar[FTRD] & 1<<1) != 0)
			iprint("operand invalid, ");
		if((dmar[FTRD] & 1<<0) != 0)
			iprint("undefined instruction, ");
		iprint("\n");
	}
	for(i = 0; i < 8; i++){
		if((dmar[FSRC] & 1<<i) == 0)
			continue;
		iprint("dma: channel %d fault\n", i);
		iprint("code = %.8p,   PC   = %.8ulx\n", code, CPC(i));
		iprint("CCRn = %.8ulx, CSRn = %.8ulx\n", CCR(i), CSR(i));
		iprint("LC0  = %.2ulx, LC1  = %.2ulx\n", LC0(i), LC1(i));
		if((FTR(i) & 1<<31) != 0)
			iprint("insufficient resources, ");
		if((FTR(i) & 1<<30) != 0)
			iprint("debug instruction, ");
		if((FTR(i) & 1<<18) != 0)
			iprint("read error, ");
		if((FTR(i) & 1<<17) != 0)
			iprint("write error, ");
		if((FTR(i) & 1<<16) != 0)
			iprint("instruction fetch error, ");
		if((FTR(i) & 1<<13) != 0)
			iprint("FIFO underflow, ");
		if((FTR(i) & 1<<12) != 0)
			iprint("FIFO error, ");
		if((FTR(i) & 1<<7) != 0)
			iprint("CCRn security violation, ");
		if((FTR(i) & 1<<6) != 0)
			iprint("peripheral security violation, ");
		if((FTR(i) & 1<<5) != 0)
			iprint("event security violation, ");
		if((FTR(i) & 1<<4) != 0)
			iprint("DMAGO security violation, ");
		if((FTR(i) & 1<<1) != 0)
			iprint("operand invalid, ");
		if((FTR(i) & 1<<0) != 0)
			iprint("undefined instruction, ");
		iprint("\n");
	}
	panic("DMA fault");
}

void
dmalink(void)
{
	dmar[INTEN] = 1;
	intrenable(DMAIRQ0, dmairq, nil, LEVEL, "dma");
	intrenable(DMAABORTIRQ, dmaabort, nil, LEVEL, "dma_abort");
}
