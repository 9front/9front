#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

enum {
	CTRL = 0,
	MODE,
	IRQEN,
	IRQDIS,
	MASK,
	INTSTAT,
	RXFIFOLVL = 0x20/4,
	CHANSTAT = 0x2C/4,
	FIFO = 0x30/4,
};

enum {
	TXFULL = 1<<4,
	TXEMPTY = 1<<3,
	RXEMPTY = 1<<1,
	RXTRIG = 1<<0,
};

typedef struct Ctlr {
	Lock;
	ulong *r;
	int irq, iena;
} Ctlr;

extern PhysUart zynqphysuart;

static Ctlr zctlr[1] = {
	{
		.r = (void *) VMAP,
		.irq = UART1IRQ,
	}
};

static Uart zuart[1] = {
	{
		.regs = &zctlr[0],
		.name = "UART1",
		.freq = 25000000,
		.phys = &zynqphysuart,
		.console = 1,
		.baud = 115200,
	}
};

void
uartconsinit(void)
{
	consuart = zuart;
	uartctl(consuart, "l8 pn s1");
}

static Uart *
zuartpnp(void)
{
	return zuart;
}

static void
zuartkick(Uart *uart)
{
	Ctlr *ct;
	int i;

	ct = uart->regs;
	for(i = 0; i < 128; i++){
		if((ct->r[CHANSTAT] & TXFULL) != 0)
			break;
		if(uart->op >= uart->oe && uartstageoutput(uart) == 0)
			break;
		ct->r[FIFO] = *uart->op++;
	}
}

static void
zuartintr(Ureg *, void *arg)
{
	Uart *uart;
	Ctlr *ct;
	int c;
	ulong fl;
	
	uart = arg;
	ct = uart->regs;
	fl = ct->r[INTSTAT] & ct->r[MASK];
	ct->r[INTSTAT] = fl;
	if((fl & RXTRIG) != 0)
		while((ct->r[CHANSTAT] & RXEMPTY) == 0){
			c = ct->r[FIFO];
			uartrecv(uart, c);
		}
	if((fl & TXEMPTY) != 0)
		uartkick(uart);
}

static void
zuartenable(Uart *uart, int ie)
{
	Ctlr *ctlr;
	
	ctlr = uart->regs;
	ilock(ctlr);
	while((ctlr->r[CHANSTAT] & TXEMPTY) == 0)
		;
	ctlr->r[IRQDIS] = -1;
	ctlr->r[RXFIFOLVL] = 1;
	if(ie){
		if(!ctlr->iena){
			intrenable(ctlr->irq, zuartintr, uart, LEVEL, uart->name);
			ctlr->iena = 1;
		}
		ctlr->r[IRQEN] = RXTRIG | TXEMPTY;
	}
	iunlock(ctlr);
}

static int
zuartgetc(Uart *uart)
{
	Ctlr *c;
	
	c = uart->regs;
	while((c->r[CHANSTAT] & RXEMPTY) != 0)
		;
	return c->r[FIFO];
}

static void
zuartputc(Uart *uart, int c)
{
	Ctlr *ct;
	
	ct = uart->regs;
	while((ct->r[CHANSTAT] & TXFULL) != 0)
		;
	ct->r[FIFO] = c;
	return;
}

int
zuartbits(Uart *uart, int n)
{
	Ctlr *ct;
	
	ct = uart->regs;
	ct->r[MODE] &= ~6;
	switch(n){
	case 8:
		return 0;
	case 7:
		ct->r[MODE] |= 4;
		return 0;
	case 6:
		ct->r[MODE] |= 6;
		return 0;
	default:
		return -1;
	}
}

int
zuartbaud(Uart *, int n)
{
	print("uart baud %d\n", n);
	return 0;
}

int
zuartparity(Uart *uart, int p)
{
	Ctlr *ct;
	
	ct = uart->regs;
	switch(p){
	case 'o':
		ct->r[MODE] = ct->r[MODE] & ~0x38 | 0x08;
		return 0;
	case 'e':
		ct->r[MODE] = ct->r[MODE] & ~0x38;
		return 0;
	case 'n':
		ct->r[MODE] = ct->r[MODE] & 0x38 | 0x20;
		return 0;
	default:
		return -1;
	}
}

void
zuartnop(Uart *, int)
{
}

int
zuartnope(Uart *, int)
{
	return -1;
}


PhysUart zynqphysuart = {
	.pnp = zuartpnp,
	.enable = zuartenable,
	.kick = zuartkick,
	.getc = zuartgetc,
	.putc = zuartputc,
	.bits = zuartbits,
	.baud = zuartbaud,
	.parity = zuartparity,
	
	.stop = zuartnope,
	.rts = zuartnop,
	.dtr = zuartnop,
	.dobreak = zuartnop,
	.fifo = zuartnop,
	.power = zuartnop,
	.modemctl = zuartnop,
};
