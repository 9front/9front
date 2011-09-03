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
	while((uart[17] & 1) == 0){
		if(u->op >= u->oe)
			if(uartstageoutput(u) == 0)
				break;
		uart[0] = *u->op++;
	}
	if(u->op < u->oe || qlen(u->oq))
		uart[1] |= (1<<1);
	else
		uart[1] &= ~(1<<1);
}

void
omapinterrupt(Ureg *)
{
	ulong st;

	st = uart[2];
	if((st & 1) != 0)
		return;
	switch((st >> 1) & 0x1F){
	case 1:
		uartkick(&puart);
		break;
	case 2:
	case 6:
		while(uart[5] & 1)
			uartrecv(&puart, uart[0]);
		break;
	default:
		print("unknown UART interrupt %d\n", (st>>1) & 0x1F);
		uartkick(&puart);
	}
}

static void
omapenable(Uart *, int ie)
{
	while(uart[5] & (1<<6))
		;
	if(ie){
		irqroute(74, omapinterrupt);
		uart[1] = (1<<0);
	//	uart[0x10] |= (1<<3);
	}
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

static void
omaprts(Uart *, int)
{
}

PhysUart omapphysuart = {
	.name = "omap4430 uart",
	.pnp = omappnp,
	.getc = omapgetc,
	.putc = omapputc,
	.enable = omapenable,
	.kick = omapkick,
	.rts = omaprts,
};

void
uartinit(void)
{
	consuart = &puart;
	puart.console = 1;
}
