/*
 * bcm2835 PL011 uart
 */

#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

enum {
	DR	=	0x00>>2,
	RSRECR	=	0x04>>2,
	FR	=	0x18>>2,
		TXFE	= 1<<7,
		RXFF	= 1<<6,
		TXFF	= 1<<5,
		RXFE	= 1<<4,
		BUSY	= 1<<3,

	ILPR	=	0x20>>2,
	IBRD	=	0x24>>2,
	FBRD	=	0x28>>2,
	LCRH	=	0x2c>>2,
		WLENM	= 3<<5,
		WLEN8	= 3<<5,
		WLEN7	= 2<<5,
		WLEN6	= 1<<5,
		WLEN5	= 0<<5,
		FEN	= 1<<4,	/* fifo enable */
		STP2	= 1<<3,	/* 2 stop bits */
		EPS	= 1<<2,	/* even parity select */
		PEN	= 1<<1,	/* parity enabled */
		BRK	= 1<<0,	/* send break */

	CR	=	0x30>>2,
		CTSEN	= 1<<15,
		RTSEN	= 1<<14,
		RTS	= 1<<11,
		RXE	= 1<<9,
		TXE	= 1<<8,
		LBE	= 1<<7,
		UARTEN	= 1<<0,
		
	IFLS	=	0x34>>2,
	IMSC	=	0x38>>2,
		TXIM	= 1<<5,
		RXIM	= 1<<4,

	RIS	=	0x3c>>2,
	MIS	=	0x40>>2,
	ICR	=	0x44>>2,
	DMACR	=	0x48>>2,
	ITCR	=	0x80>>2,
	ITIP	=	0x84>>2,
	ITOP	=	0x88>>2,
	TDR	=	0x8c>>2,
};

extern PhysUart pl011physuart;

static Uart pl011uart = {
	.regs	= (u32int*)(VIRTIO+0x201000),
	.name	= "uart0",
	.freq	= 250000000,
	.baud	= 115200,
	.phys	= &pl011physuart,
};

static Uart*
pnp(void)
{
	return &pl011uart;
}

static void
interrupt(Ureg*, void *arg)
{
	Uart *uart = arg;
	u32int *reg = (u32int*)uart->regs;

	coherence();
	while((reg[FR] & RXFE) == 0)
		uartrecv(uart, reg[DR] & 0xFF);
	if((reg[FR] & TXFF) == 0)
		uartkick(uart);
	reg[ICR] = 1<<5 | 1<<6 | 1<<7 | 1<<8 | 1<<9 | 1<<10;
	coherence();
}

static void
disable(Uart *uart)
{
	u32int *reg = (u32int*)uart->regs;

	/* disable interrupt */
	reg[IMSC] = 0;
	coherence();

	/* clear interrupt */
	reg[ICR] = 1<<5 | 1<<6 | 1<<7 | 1<<8 | 1<<9 | 1<<10;
	coherence();

	/* wait for last transmission to complete */
	while((reg[FR] & BUSY) != 0)
		delay(1);

	/* disable uart */
	reg[CR] = 0;
	coherence();

	/* flush rx fifo */
	reg[LCRH] &= ~FEN;
	coherence();
}

static void
uartoff(Uart *uart)
{
	u32int *reg = (u32int*)uart->regs;
	u32int im;

	im = reg[IMSC];
	disable(uart);
	reg[IMSC] = im;
}

static void
uarton(Uart *uart)
{
	u32int *reg = (u32int*)uart->regs;

	/* enable fifo */
	reg[LCRH] |= FEN;
	coherence();

	/* enable uart */
	reg[CR] = UARTEN | RXE | TXE;
	coherence();
}

static void
enable(Uart *uart, int ie)
{
	u32int *reg = (u32int*)uart->regs;

	disable(uart);
	if(ie){
		intrenable(IRQuart, interrupt, uart, BUSUNKNOWN, uart->name);
		reg[IMSC] = TXIM|RXIM;
	}
	uarton(uart);
}

static void
linectl(Uart *uart, u32int set, u32int clr)
{
	u32int *reg = (u32int*)uart->regs;

	if(uart->enabled)
		uartoff(uart);

	reg[LCRH] = set | (reg[LCRH] & ~clr);

	if(uart->enabled)
		uarton(uart);
}

static void
kick(Uart *uart)
{
	u32int *reg = (u32int*)uart->regs;

	if(uart->blocked)
		return;
	coherence();
	while((reg[FR] & TXFF) == 0){
		if(uart->op >= uart->oe && uartstageoutput(uart) == 0)
			break;
		reg[DR] = *(uart->op++);
	}
	coherence();
}

static void
dobreak(Uart *uart, int ms)
{
	linectl(uart, BRK, 0);
	delay(ms);
	linectl(uart, 0, BRK);
}

static int
baud(Uart *uart, int n)
{
	u32int *reg = (u32int*)uart->regs;

	if(uart->freq <= 0 || n <= 0)
		return -1;

	if(uart->enabled)
		uartoff(uart);

	reg[IBRD] = (uart->freq >> 4) / n;
	reg[FBRD] = (uart->freq >> 4) % n;

	if(uart->enabled)
		uarton(uart);

	uart->baud = n;
	return 0;
}

static int
bits(Uart *uart, int n)
{
	switch(n){
	case 8:
		linectl(uart, WLEN8, WLENM);
		break;
	case 7:
		linectl(uart, WLEN7, WLENM);
		break;
	case 6:
		linectl(uart, WLEN6, WLENM);
		break;
	case 5:
		linectl(uart, WLEN5, WLENM);
		break;
	default:
		return -1;
	}
	uart->bits = n;
	return 0;
}

static int
stop(Uart *uart, int n)
{
	switch(n){
	case 1:
		linectl(uart, 0, STP2);
		break;
	case 2:
		linectl(uart, STP2, 0);
		break;
	default:
		return -1;
	}
	uart->stop = n;
	return 0;
}

static int
parity(Uart *uart, int n)
{
	switch(n){
	case 'n':
		linectl(uart, 0, PEN);
		break;
	case 'e':
		linectl(uart, EPS|PEN, 0);
		break;
	case 'o':
		linectl(uart, PEN, EPS);
		break;
	default:
		return -1;
	}
	uart->parity = n;
	return 0;
}

static void
modemctl(Uart *uart, int on)
{
	uart->modem = on;
}

static void
rts(Uart*, int)
{
}

static long
status(Uart *uart, void *buf, long n, long offset)
{
	char *p;

	p = malloc(READSTR);
	if(p == nil)
		error(Enomem);
	snprint(p, READSTR,
		"b%d\n"
		"dev(%d) type(%d) framing(%d) overruns(%d) "
		"berr(%d) serr(%d)\n",

		uart->baud,
		uart->dev,
		uart->type,
		uart->ferr,
		uart->oerr,
		uart->berr,
		uart->serr
	);
	n = readstr(offset, buf, n, p);
	free(p);

	return n;
}

static void
donothing(Uart*, int)
{
}

static void
putc(Uart *uart, int c)
{
	u32int *reg = (u32int*)uart->regs;

	while((reg[FR] & TXFF) != 0)
		;
	reg[DR] = c & 0xFF;
}

static int
getc(Uart *uart)
{
	u32int *reg = (u32int*)uart->regs;

	while((reg[FR] & RXFE) != 0)
		;
	return reg[DR] & 0xFF;
}

PhysUart pl011physuart = {
	.name		= "pl011",
	.pnp		= pnp,
	.enable		= enable,
	.disable	= disable,
	.kick		= kick,
	.dobreak	= dobreak,
	.baud		= baud,
	.bits		= bits,
	.stop		= stop,
	.parity		= parity,
	.modemctl	= donothing,
	.rts		= rts,
	.dtr		= donothing,
	.status		= status,
	.fifo		= donothing,
	.getc		= getc,
	.putc		= putc,
};
