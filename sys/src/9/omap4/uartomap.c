#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

extern ulong *uart;
extern PhysUart omapphysuart;
static Uart puart = {
	.phys = &omapphysuart,
	.bits = 8,
	.stop = 1,
	.baud = 115200,
	.parity = 'n',
};

static Uart *
omappnp(void)
{
	return &puart;
}

static void
omapkick(Uart *u)
{
	int x;
	
	x = splhi();
	while((uart[17] & 1) == 0){
		if(u->op >= u->oe)
			if(uartstageoutput(u) == 0){
				uart[1] &= ~(1<<1);
				break;
			}
		uart[0] = *u->op++;
		uart[1] |= (1<<1);
	}
	splx(x);
}

void
omapinterrupt(Ureg *, void *)
{
	ulong st;

	st = uart[2];
	if((st & 1) != 0)
		return;
	switch((st >> 1) & 0x1F){
	case 0:
	case 16:
		puart.cts = (uart[6] & (1<<4)) != 0;
		puart.dsr = (uart[6] & (1<<5)) != 0;
		puart.dcd = (uart[6] & (1<<7)) != 0;
		break;
	case 1:
		uartkick(&puart);
		break;
	case 2:
	case 6:
		while(uart[5] & 1)
			uartrecv(&puart, uart[0]);
		break;
	default:
		print("unknown UART interrupt %uld\n", (st>>1) & 0x1F);
		uartkick(&puart);
	}
}

static void
omapenable(Uart *u, int ie)
{
	while(uart[5] & (1<<6))
		;
	if(ie){
		irqroute(74, omapinterrupt, u);
		uart[1] = (1<<0);
	}
}

static void
omapdisable(Uart *)
{
	uart[1] = 0;
}

static void
omapdobreak(Uart *, int ms)
{
	if(ms <= 0)
		ms = 200;
	
	uart[3] |= (1<<6);
	tsleep(&up->sleep, return0, 0, ms);
	uart[3] &= ~(1<<6);
}

static int
omapbaud(Uart *u, int baud)
{
	int val;

	if(baud <= 0)
		return -1;

	val = (48000000 / 16) / baud;
	uart[3] |= (1<<7);
	uart[0] = val & 0xFF;
	uart[1] = (val >> 8) & 0xFF;
	uart[3] &= ~(1<<7);
	u->baud = baud;
	return 0;
}

static int
omapbits(Uart *u, int bits)
{
	if(bits < 5 || bits > 8)
		return -1;
	
	uart[3] = (uart[3] & ~3) | (bits - 5);
	u->bits = bits;
	return 0;
}

static int
omapstop(Uart *u, int stop)
{
	if(stop < 1 || stop > 2)
		return -1;
	
	uart[3] &= ~4;
	if(stop == 2)
		uart[3] |= 4;
	u->stop = stop;
	return 0;
}

static int
omapparity(Uart *u, int parity)
{
	uart[3] &= ~0x38;
	switch(parity){
	case 'n':
		break;
	case 'o':
		uart[3] |= (1<<3);
	case 'e':
		uart[3] |= (1<<3) | (1<<4);
		break;
	default:
		return -1;
	}
	u->parity = parity;
	return 0;
}

static void
omapmodemctl(Uart *u, int on)
{
	if(on){
		u->modem = 1;
		u->cts = (uart[6] & (1<<4)) != 0;
		uart[1] |= (1<<6);
	}else{
		u->modem = 0;
		u->cts = 1;
	}
}

static void
omaprts(Uart *, int i)
{
	uart[4] = (uart[4] & ~2) | (i << 1);
}

static void
omapdtr(Uart *, int i)
{
	uart[4] = (uart[4] & ~1) | i;
}

static long
omapstatus(Uart* u, void* buf, long n, long offset)
{
	char *p;
	ulong msr;

	msr = uart[6];
	p = malloc(READSTR);
	snprint(p, READSTR,
		"b%d c%d d%d e%d l%d m%d p%c r%d s%d\n"
		"dev(%d) type(%d) framing(%d) overruns(%d) "
		"berr(%d) serr(%d)%s%s%s%s\n",

		u->baud,
		u->hup_dcd, 
		u->dsr,
		u->hup_dsr,
		u->bits,
		u->modem,
		u->parity,
		(uart[3] & 2) != 0,
		u->stop,

		u->dev,
		u->type,
		u->ferr,
		u->oerr,
		u->berr,
		u->serr,
		(msr & (1<<4)) ? " cts": "",
		(msr & (1<<5)) ? " dsr": "",
		(msr & (1<<7)) ? " dcd": "",
		(msr & (1<<6)) ? " ri": ""
	);
	n = readstr(offset, buf, n, p);
	free(p);

	return n;
}

static int
omapgetc(Uart *)
{
	while((uart[5] & 1) == 0)
		;
	return uart[0];
}

static void
omapputc(Uart *, int c)
{
	while(uart[17] & 1)
		;
	uart[0] = c;
}

PhysUart omapphysuart = {
	.name = "omap4430 uart",
	.pnp = omappnp,
	.getc = omapgetc,
	.putc = omapputc,
	.enable = omapenable,
	.disable = omapdisable,
	.kick = omapkick,
	.rts = omaprts,
	.parity = omapparity,
	.baud = omapbaud,
	.bits = omapbits,
	.stop = omapstop,
	.modemctl = omapmodemctl,
	.dtr = omapdtr,
	.status = omapstatus,
};

void
uartinit(void)
{
	consuart = &puart;
	puart.console = 1;
}
