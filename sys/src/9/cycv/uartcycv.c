#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

enum {
	RBR = 0,
	IER,
	FCR,
	LCR,
	MCR,
	LSR,
	MSR,
	SCR,
	
	IIR = FCR,
};

enum {
	LSR_THRE = 1<<5,
	LSR_DR = 1<<0,
	
	ENTXIRQ = 1<<1,
	ENRXIRQ = 1<<0
};

typedef struct Ctlr {
	Lock;
	ulong *r;
	int irq, iena;
} Ctlr;

Uart* uartenable(Uart *);

extern PhysUart cycvphysuart;

static Ctlr vctlr[1] = {
	{
		.r = (void *) UART_BASE,
		.irq = UART0IRQ,
	}
};

static Uart vuart[1] = {
	{
		.regs = &vctlr[0],
		.name = "UART1",
		.freq = 25000000,
		.phys = &cycvphysuart,
		.console = 1,
		.baud = 115200,
	}
};

void
uartinit(void)
{
	consuart = vuart;
}

static Uart *
vuartpnp(void)
{
	return vuart;
}

static void
vuartkick(Uart *uart)
{
	Ctlr *ct;
	int i;

	if(uart->blocked)
		return;
	ct = uart->regs;
	if((ct->r[LSR] & LSR_THRE) == 0)
		return;
	for(i = 0; i < 128; i++){
		if(uart->op >= uart->oe && uartstageoutput(uart) == 0)
			break;
		ct->r[RBR] = *uart->op++;
	}
}

static void
vuartintr(Ureg *, void *arg)
{
	Uart *uart;
	Ctlr *c;
	int ch, f;
	
	uart = arg;
	c = uart->regs;
	for(;;){
		f = c->r[IIR] & 15;
		switch(f){
		case 6: USED(c->r[LSR]); break;
		case 4: case 8:
			while((c->r[LSR] & LSR_DR) != 0){
				ch = c->r[RBR];
				uartrecv(uart, ch);
			}
			break;
		case 2:
			vuartkick(uart);
			break;
		default:
			return;
		}
	}
}

static void
vuartenable(Uart *uart, int ie)
{
	Ctlr *c;
	
	c = uart->regs;
	ilock(c);
	while((c->r[LSR] & LSR_THRE) == 0)
		;
	c->r[LCR] = 0x03;
	c->r[FCR] = 0x1;
	c->r[IER] = 0x0;
	if(ie){
		if(!c->iena){
			intrenable(c->irq, vuartintr, uart, LEVEL, uart->name);
			c->iena = 1;
		}
		c->r[IER] = ENTXIRQ | ENRXIRQ;
	}
	iunlock(c);
}

static int
vuartgetc(Uart *uart)
{
	Ctlr *c;
	
	c = uart->regs;
	while((c->r[LSR] & LSR_DR) == 0)
		;
	return c->r[RBR];
}

static void
vuartputc(Uart *uart, int c)
{
	Ctlr *ct;
	
	ct = uart->regs;
	while((ct->r[LSR] & LSR_THRE) == 0)
		;
	ct->r[RBR] = c;
	return;
}

int
uartconsole(void)
{
	Uart *uart = vuart;

	if(up == nil)
		return -1;

	if(uartenable(uart) != nil){
		serialoq = uart->oq;
		uart->opens++;
		consuart = uart;
	}
	return 0;
}

int
vuartbits(Uart *uart, int n)
{
	Ctlr *c;
	
	c = uart->regs;
	switch(n){
	case 5: c->r[LCR] = c->r[LCR] & ~3 | 0; return 0;
	case 6: c->r[LCR] = c->r[LCR] & ~3 | 1; return 0;
	case 7: c->r[LCR] = c->r[LCR] & ~3 | 2; return 0;
	case 8: c->r[LCR] = c->r[LCR] & ~3 | 3; return 0;
	default:
		return -1;
	}
}

int
vuartbaud(Uart *, int n)
{
	print("uart baud %d\n", n);
	return 0;
}

int
vuartparity(Uart *uart, int p)
{
	Ctlr *c;
	
	c = uart->regs;
	switch(p){
	case 'n': c->r[LCR] = c->r[LCR] & ~0x38; return 0;
	case 'o': c->r[LCR] = c->r[LCR] & ~0x38 | 0x08; return 0;
	case 'e': c->r[LCR] = c->r[LCR] & ~0x38 | 0x18; return 0;
	default:
		return -1;
	}
}

void
vuartnop(Uart *, int)
{
}

int
vuartnope(Uart *, int)
{
	return -1;
}


PhysUart cycvphysuart = {
	.pnp = vuartpnp,
	.enable = vuartenable,
	.kick = vuartkick,
	.getc = vuartgetc,
	.putc = vuartputc,
	.bits = vuartbits,
	.baud = vuartbaud,
	.parity = vuartparity,
	
	.stop = vuartnope,
	.rts = vuartnop,
	.dtr = vuartnop,
	.dobreak = vuartnop,
	.fifo = vuartnop,
	.power = vuartnop,
	.modemctl = vuartnop,
};
