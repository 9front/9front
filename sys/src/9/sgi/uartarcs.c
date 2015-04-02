/*
 * ARCS console.
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"

extern PhysUart arcsphysuart;

static Uart arcsuart = {
	.name = "arcs",
	.freq = 1843200,
	.phys = &arcsphysuart,
};

static Lock arcslock;

void
arcsputc(char c)
{
	int r;

	r = 0;
	ilock(&arcslock);
	arcs(0x6c, 1, &c, 1, &r);
	iunlock(&arcslock);
}

int
arcsgetc(void)
{
	int c, r;
	uchar b;

	r = 0;
	c = -1;
	ilock(&arcslock);
	if(arcs(0x68, 0) == 0)
	if(arcs(0x64, 0, &b, 1, &r) == 0)
	if(r == 1)
		c = b;
	iunlock(&arcslock);
	return c;
}

void
arcsproc(void*)
{
	int c;

	while(waserror())
		;
	for(;;){
		tsleep(&up->sleep, return0, nil, 50);
		c = arcsgetc();
		if(c < 0)
			continue;
		uartrecv(&arcsuart, c);
	}
}

/*
 * Send queued output to console
 */
static void
kick(Uart *uart)
{
	int n;

	for(n=0; uart->op < uart->oe || uartstageoutput(uart) > 0; uart->op += n){
		n = uart->oe - uart->op;
		if(n <= 0 || !canlock(&arcslock))
			break;
		if(arcs(0x6c, 1, uart->op, n, &n) != 0)
			n = -1;
		unlock(&arcslock);
		if(n <= 0)
			break;
	}
}

static void
interrupt(Ureg*, void *)
{
}

static Uart*
pnp(void)
{
	return &arcsuart;
}

static void
enable(Uart*, int)
{
}

static void
disable(Uart*)
{
}

static void
donothing(Uart*, int)
{
}

static int
donothingint(Uart*, int)
{
	return 0;
}

static int
baud(Uart *uart, int n)
{
	if(n <= 0)
		return -1;

	uart->baud = n;
	return 0;
}

static int
bits(Uart *uart, int n)
{
	switch(n){
	case 7:
	case 8:
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
	if(n != 1)
		return -1;
	uart->stop = n;
	return 0;
}

static int
parity(Uart *uart, int n)
{
	if(n != 'n')
		return -1;
	uart->parity = n;
	return 0;
}

static long
status(Uart *, void *, long, long)
{
	return 0;
}

void
uartarcsputc(Uart*, int c)
{
	arcsputc(c);
}

int
uartarcsgetc(Uart*)
{
	return arcsgetc();
}

PhysUart arcsphysuart = {
	.name		= "arcsuart",

	.pnp		= pnp,
	.enable		= enable,
	.disable	= disable,
	.kick		= kick,
	.dobreak	= donothing,
	.baud		= baud,
	.bits		= bits,
	.stop		= stop,
	.parity		= parity,
	.modemctl	= donothing,
	.rts		= donothing,
	.dtr		= donothing,
	.status		= status,
	.fifo		= donothing,

	.getc		= uartarcsgetc,
	.putc		= uartarcsputc,
};

void
arcsconsinit(void)
{
	consuart = &arcsuart;
	consuart->console = 1;
}
