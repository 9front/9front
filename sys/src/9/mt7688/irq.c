/*
 *  Interrupt Handling for the MT7688
 */
#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"ureg.h"
#include	"io.h"
#include	"../port/error.h"



/* map the irq number to the interrupt controller */
static const int irq2inc[32] = {
	/* cpu based interrupts */
	[IRQsw1]	=	-1,
	[IRQsw2]	=	-1,
	[IRQlow]	=	-1,
	[IRQhigh]	=	-1,
	[IRQpci]	=	-1,
	[IRQethr]	=	-1,
	[IRQwifi]	=	-1,
	[IRQtimer]	=	-1,

	/* irqs on the SoC interrupt controller */
	[IRQsys]	=	INC_SYSCTL,
	[IRQtimer0]	=	INC_TIMER0,
	[IRQwdog]	=	INC_WDOG,
	[IRQillacc]	=	INC_ILLACC,
	[IRQpcm]	=	INC_PCM,
	[IRQuartf]	=	INC_UARTF,
	[IRQgpio]	=	INC_GPIO,
	[IRQdma]	=	INC_DMA,
	[IRQnand]	=	INC_NAND,
	[IRQperf]	=	INC_PERF,
	[IRQi2s]	=	INC_I2S,
	[IRQspi]	=	INC_SPI,
	[IRQuartl]	=	INC_UARTL,
	[IRQcrypto]	=	INC_CRYPTO,
//	[IRQsdhc]	=	INC_SDHC,
//	[IRQr2p]	=	INC_R2P,
	[IRQethsw]	=	INC_ETHSW,
	[IRQusbh]	=	INC_USBH,
	[IRQusbd]	=	INC_USBD,
};


static const int inc2irq[32] = {
	[INC_SYSCTL]	=	IRQsys,
	[INC_TIMER0]	=	IRQtimer0,
	[INC_WDOG]		=	IRQwdog,
	[INC_ILLACC]	=	IRQillacc,
	[INC_PCM]		=	IRQpcm,
	[INC_UARTF]		=	IRQuartf,
	[INC_GPIO]		=	IRQgpio,
	[INC_DMA]		=	IRQdma,
	[INC_NAND]		=	IRQnand,
	[INC_PERF]		=	IRQperf,
	[INC_I2S]		=	IRQi2s,
	[INC_SPI]		=	IRQspi,
	[INC_UARTL]		=	IRQuartl,
	[INC_CRYPTO]	=	IRQcrypto,
//	[INC_SDHC]		=	IRQsdhc,
//	[INC_R2P]		=	IRQr2p,
	[INC_ETHSW]		=	IRQethsw,
	[INC_USBH]		=	IRQusbh,
	[INC_USBD]		=	IRQusbd,
};




typedef struct Handler Handler;

struct Handler {
	Handler *next;
	void 	(*f)(Ureg*, void *);
	void	*arg;
	int		irq;
};

static Lock intrlock;
static Handler handlers[IRQmax+1];


void incintr(Ureg*, void*);



static u32int
incread(int offset)
{
	return *IO(u32int, (IRQBASE + offset));
}


static void
incwrite(int offset, u32int val)
{
	*IO(u32int, (IRQBASE + offset)) = val;
}

/*
 * called by main(), clears all the irq's
 * sets SoC interrupt controller to relay
 * IRQs through CPU interrupts 2 and 3
 */

void
intrinit(void)
{
	incwrite(IRQ_MASK_CLR, 0xFFFFFFFF);

	intrenable(IRQlow, incintr, (void *)0, 0, "inclow");
//	intrenable(IRQhigh, incintr, (void *)1, 1, "inchigh");
}


/* called by drivers to setup irq's */
void
intrenable(int irq, void (*f)(Ureg*, void *), void *arg, int priority, char *name)
{
	Handler *hp;
	u32int r;


	if(irq > IRQmax || irq < 0)
		panic("intrenable: %s gave bad irq number of %d", name, irq);

	/* debugging */
	if(irq == 0 || irq == 1)
		iprint("software irq enabled?");

	hp = &handlers[irq];
	ilock(&intrlock);

	if(hp->f != nil) {
		for(; hp->next != nil; hp = hp->next)
			;
		if((hp->next = xalloc(sizeof *hp)) == nil)
			panic("intrenable: out of memory");
		hp = hp->next;
		hp->next = nil;
	}

	hp->f = f;
	hp->arg = arg;
	hp->irq = irq;

	iunlock(&intrlock);

	if(irq > IRQtimer) {
		r = incread(FIQ_SEL);
		r |= (priority << irq2inc[irq]);
		incwrite(FIQ_SEL, r);
		incwrite(IRQ_MASK_SET, (1 << irq2inc[irq]));
	} else {
		intron(INTR0 << irq);
	}

}



void
intrdisable(int irq, void (*)(Ureg*, void *), void*, int, char *name)
{
	if(irq > IRQmax || irq < 0)
		panic("intrdisable: %s gave bad irq number of %d", name, irq);

	if(irq > IRQtimer) {
		incwrite(IRQ_MASK_CLR, (1 << irq2inc[irq]));
	} else {
		introff(INTR0 << irq);
	}
}


/* called by trap to handle requests, returns true if a clock interrupt */
int
intr(Ureg* ur)
{	
	ulong cause, mask;
	int clockintr;
	Handler *hh, *hp;

	m->intr++;
	clockintr = 0;
	/*
	 * ignore interrupts that we have disabled, even if their cause bits
	 * are set.
	 */
	cause = ur->cause & ur->status & INTMASK;
	cause &= ~(INTR1|INTR0);		/* ignore sw interrupts */

	if (cause == 0)
		iprint("spurious interrupt\n");

	if(cause & INTR7){
		clock(ur);
		cause &= ~INTR7;
		clockintr = 1;
	}

//	iprint("INTR %luX\n", cause);

	hh = &handlers[2];
	for(mask = INTR2; cause != 0 && mask < INTR7; mask <<= 1){
		if(cause & mask){
			for(hp = hh; hp != nil; hp = hp->next){
				if(hp->f != nil){
					hp->f(ur, hp->arg);
					cause &= ~mask;
				}
			}
		}
		hh++;
	}
	if(cause != 0)
		iprint("unhandled interrupts %lux\n", cause);

	return clockintr;
}


/* off to handle requests for the SoC interrupt controller */
/*
 * the interrupts controller on the mt7688 SoC can be mapped to 
 * either CPU interrupt 2 or 3.  So when those are tripped, 
 * this code then checks the secondary interrupt controller 
 * to see which IRQ it has.  The controller defines CPU INTR2 
 * as "low priority" IRQ, and INTR3 as "high priority" FIQ.
 */

void
incintr(Ureg *ureg, void *arg)
{
	u32int p;
	u32int reg;
	u32int pending;
	u32int mask;
	Handler *hh, *hp;


	p = (uintptr)arg;
	reg = (p == 0) ? IRQ_STAT : FIQ_STAT;
	pending = incread(reg);

	hh = &handlers[8];
	for(mask = 1 ; pending != 0 && mask < 0x80000000; mask <<= 1) {
		if(pending & mask) {
			for(hp = hh; hp != nil; hp = hp->next) {
				if(hp->f != nil) {
					hp->f(ureg, hp->arg);
					pending &= ~mask;
				}
			}
		}
		hh++;
	}

	if(pending != 0){
		iprint("unhandled inc interrupts %uX\n", pending);
		delay(2000);
	}
}


void
intrclear(int irq)
{
	incwrite(IRQ_EOI, 1 << irq2inc[irq]);
}


void
intrshutdown(void)
{
	introff(INTMASK);
	incwrite(IRQ_MASK_CLR, 0xFFFF);
	coherence();

}

/*
 * left over debugging stuff
 */

ulong
incraw(void)
{
	return incread(INT_PURE);
}

ulong
incmask(void)
{
	return incread(IRQ_MASK);
}

ulong
incstat(void)
{
	return incread(IRQ_STAT);
}

ulong
incsel(void)
{
	return incread(IRQ_SEL0);
}

