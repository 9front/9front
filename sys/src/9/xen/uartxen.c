/*
 * xencons.c
 *	Access to xen consoles.
 */

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "../pc/io.h"

extern PhysUart xenphysuart;

static Uart xenuart = {
	.name = "xencons",
	.freq = 1843200,
	.phys = &xenphysuart,
};

struct {
	struct xencons_interface *intf;
	int evtchn;
	Lock txlock;
} xencons;

/*
 * Debug print to xen "emergency console".
 * Output only appears if xen is built with verbose=y
 */
void
dprint(char *fmt, ...)
{
	int n;
	va_list arg;
	char buf[PRINTSIZE];

	va_start(arg, fmt);
	n = vseprint(buf, buf+sizeof(buf), fmt, arg) - buf;
	va_end(arg);
	HYPERVISOR_console_io(CONSOLEIO_write, n, buf);
}

static void kick(Uart*);
/*
 * Emit a string to the guest OS console, bypassing the queue
 *   - before serialoq is initialised
 *   - when rdb is activated
 *   - from iprint() for messages from interrupt routines
 * If ring is full, just throw extra output away.
 */
void
xenuartputs(char *s, int n)
{
	struct xencons_interface *con = xencons.intf;
	unsigned long prod;
	int c;

	ilock(&xencons.txlock);
	prod = con->out_prod;
	while (n-- > 0 && (prod - con->out_cons) < sizeof(con->out)) {
		c = *s++;
		/*
		if (c == '\n')
			con->out[MASK_XENCONS_IDX(prod++, con->out)] = '\r';
		*/
		con->out[MASK_XENCONS_IDX(prod++, con->out)] = c;
	}
	coherence();
	con->out_prod = prod;
	xenchannotify(xencons.evtchn);
	iunlock(&xencons.txlock);
}

/*
 * Handle channel event from console
 */
static void
interrupt(Ureg*, void *arg)
{
	char c;
	unsigned long cons;
	Uart *uart;
	struct xencons_interface *con = xencons.intf;

	uart = &xenuart;

	cons = con->in_cons;
	coherence();
	while (cons != con->in_prod) {
		c = con->in[MASK_XENCONS_IDX(cons++, con->in)];
		uartrecv(uart, c);
	}
	coherence();
	con->in_cons = cons;
	kick(nil);
}

static Uart*
pnp(void)
{
	return &xenuart;
}

static void
enable(Uart*, int ie)
{
	if(ie)
		intrenable(xencons.evtchn, interrupt, 0, BUSUNKNOWN, "Xen console");
}

static void
disable(Uart*)
{
}

/*
 * Send queued output to guest OS console
 */
static void
kick(Uart*)
{
	struct xencons_interface *con = xencons.intf;
	unsigned long prod;
	long avail, idx, n, m;

	ilock(&xencons.txlock);
	prod = con->out_prod;
	avail = sizeof(con->out) - (prod - con->out_cons);
	while (avail > 0) {
		idx = MASK_XENCONS_IDX(prod, con->out);
		m = sizeof(con->out) - idx;
		if (m > avail)
			m = avail;
		n = qconsume(serialoq, con->out+idx, m);
		if (n < 0)
			break;
		prod += n;
		avail -= n;
	}
	coherence();
	con->out_prod = prod;
	xenchannotify(xencons.evtchn);
	iunlock(&xencons.txlock);
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

void
xenputc(Uart*, int c)
{
	struct xencons_interface *con = xencons.intf;
	unsigned long prod;

	ilock(&xencons.txlock);
	prod = con->out_prod;
	if((prod - con->out_cons) < sizeof(con->out)){
		if (c == '\n')
			con->out[MASK_XENCONS_IDX(prod++, con->out)] = '\r';
		con->out[MASK_XENCONS_IDX(prod++, con->out)] = c;
	}

	coherence();
	con->out_prod = prod;
	xenchannotify(xencons.evtchn);
	iunlock(&xencons.txlock);
}

int
xengetc(Uart*)
{
	struct xencons_interface *con = xencons.intf;
	char c;

	c = 0;

	if(con->in_cons != con->in_prod){
		coherence();
		c = con->in[MASK_XENCONS_IDX(con->in_cons++, con->in)];
		if (con->in_cons == con->in_prod)
			xenchannotify(xencons.evtchn);
	}

	return c;
}

PhysUart xenphysuart = {
	.name		= "xenuart",

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

	.getc		= xengetc,
	.putc		= xenputc,
};

/* console=0 to enable */
void
xenconsinit(void)
{
	xencons.intf = (struct xencons_interface*)mmumapframe(XENCONSOLE, xenstart->console_mfn);
	xencons.evtchn = xenstart->console_evtchn;

	consuart = &xenuart;
	consuart->console = 1;
}

void
kbdenable(void)
{
	Uart *uart;
	int n;
	char *p, *cmd;

	if((p = getconf("console")) == nil)
		return;
	n = strtoul(p, &cmd, 0);
	if(p == cmd || n != 0)
		return;
	uart = &xenuart;

	(*uart->phys->enable)(uart, 0);
	uartctl(uart, "b9600 l8 pn s1");
	if(*cmd != '\0')
		uartctl(uart, cmd);

	consuart = uart;
	uart->console = 1;
}

