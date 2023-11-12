#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

enum {
	URXD	= 0x00/4,	/* UART Receiver Register */
		RX_CHARRDY	= 1<<15,
		RX_ERR		= 1<<14,
		RX_OVRRUN	= 1<<13,
		RX_FRMERR	= 1<<12,
		RX_BRK		= 1<<11,
		RX_PRERR	= 1<<10,
		RX_DATA		= 0xFF,
		
	UTXD	= 0x40/4,	/* UART Transmitter Register */
		TX_DATA		= 0xFF,

	UCR1	= 0x80/4,	/* UART Control Register 1 */
		CR1_ADEN	= 1<<15,	/* Automatic Baud Rate Detection Interrupt Enable */
		CR1_ADNR	= 1<<14,	/* Automatic Detection of Baud Rate */
		CR1_TRDYEN	= 1<<13,	/* Transmitter Ready Interrupt Enable */
		CR1_IDEN	= 1<<12,	/* Idle Condition Detected Interrupt Enable */

		CR1_ICD_SHIFT	= 10,		/* Idle Condition Detect Mask */
		CR1_ICD_MASK	= 3<<CR1_ICD_SHIFT,

		CR1_RRDYEN	= 1<<9,		/* Receiver Ready Interrupt Enable */
		CR1_RXDMAEN	= 1<<8,		/* Receive Ready DMA Enable */
		CR1_IREN	= 1<<7,		/* Infrared Interface Enable */
		CR1_TXMPTYEN	= 1<<6,		/* Transmitter Empty Interrupt Enable */
		CR1_RTSDEN	= 1<<5,		/* RTS Delta Interrupt Enable */
		CR1_SNDBRK	= 1<<4,		/* Send BREAK */
		CR1_TXDMAEN	= 1<<3,		/* Transmitter Ready DMA Enable */
		CR1_ATDMAEN	= 1<<2,		/* Aging DMA Timer Enable */
		CR1_DOZE	= 1<<1,		/* DOZE */
		CR1_UARTEN	= 1<<0,		/* Uart Enable */

	UCR2	= 0x84/4,	/* UART Control Register 2 */
		CR2_ESCI	= 1<<15,	/* Escape Sequence Interrupt Enable */
		CR2_IRTS	= 1<<14,	/* Ignore RTS Pin */
		CR2_CTSC	= 1<<13,	/* CTS Pin Control */
		CR2_CTS		= 1<<12,	/* Clear to Send */
		CR2_ESCEN	= 1<<11,	/* Escape Enable */

		CR2_RTEC_RAISING= 0<<9,
		CR2_RTEC_FALLING= 1<<9,
		CR2_RTEC_ANY	= 2<<9,
		CR2_RTEC_MASK	= 3<<9,

		CR2_PREN	= 1<<8,		/* Parity Enable */
		CR2_PREVEN	= 0<<7,		/* Parity Even */
		CR2_PRODD	= 1<<7,		/* Parity Odd */
		CR2_STPB	= 1<<6,		/* Stop */
		CR2_WS8		= 1<<5,		/* Word Size */
		CR2_WS7		= 0<<5,
		CR2_RTSEN	= 1<<4,		/* Request to Send Interrupt Enable */
		CR2_ATEN	= 1<<3,		/* Aging Timer Enable */
		CR2_TXEN	= 1<<2,		/* Transmitter Enable */
		CR2_RXEN	= 1<<1,		/* Receiver Enable */
		CR2_SRST	= 1<<0,		/* Software Reset */

	UCR3	= 0x88/4,	/* UART Control Register 3 */
		CR3_PARERREN	= 1<<12,	/* Parity Error Interrupt Enable */
		CR3_FRAERREN	= 1<<11,	/* Frame Error Interrupt Enable */
		CR3_ADNIMP	= 1<<7,		/* Autobaud Detection Not Improved */
		CR3_RXDSEN	= 1<<6,		/* Receive Status Interrupt Enable */
		CR3_AIRINTEN	= 1<<5,		/* Asynchronous IR WAKE Interrupt Enable */
		CR3_AWAKEN	= 1<<4,		/* Asynchronous WAKE Interrupt Enable */
		CR3_RXDMUXSEL	= 1<<2,		/* RXD Muxed Input Selected */
		CR3_INVT	= 1<<1,		/* Invert TXD output in RS-232/RS-485 mode */
		CR3_ACIEN	= 1<<0,		/* Autobaud Counter Interrupt Enable */

	UCR4	= 0x8C/4,	/* UART Control Register 4 */
		CR4_CTSTL_SHIFT	= 10,		/* CTS Trigger Level */
		CR4_CTSTL_MASK	= 0x3F<<CR4_CTSTL_SHIFT,

		CR4_INVR	= 1<<9,		/* Invert RXD Input in RS-232/RS-485 Mode */
		CR4_ENIRI	= 1<<8,		/* Serial Infrared Interrupt Enable */
		CR4_WKEN	= 1<<7,		/* WAKE Interrupt Enable */
		CR4_IDDMAEN	= 1<<6,		/* DMA IDLE Condition Detected Interrupt Enable */
		CR4_IRSC	= 1<<5,		/* IR Special Case */
		CR4_LPBYP	= 1<<4,		/* Low Power Bypass */
		CR4_TCEN	= 1<<3,		/* Transmit Complete Interrupt Enable */
		CR4_BKEN	= 1<<2,		/* BREAK Condition Detected Interrupt Enable */
		CR4_OREN	= 1<<1,		/* Receiver Overrun Interrupt Enable */
		CR4_DREN	= 1<<0,		/* Receive Data Interrupt Enable */

	UFCR	= 0x90/4,	/* UART FIFO Control Register */ 
		FCR_TXTL_SHIFT	= 10,		/* Transmitter Trigger Level */
		FCR_TXTL_MASK	= 0x3F<<FCR_TXTL_SHIFT,

		FCR_RFDIV_SHIFT	= 7,		/* Reference Frequency Divider */
		FCR_RFDIV_MASK	= 0x7<<FCR_RFDIV_SHIFT,

		FCR_DCE		= 0<<6,		/* DCE/DTE mode select */
		FCR_DTE		= 1<<6,

		FCR_RXTL_SHIFT	= 0,		/* Receive Trigger Level */
		FCR_RXTL_MASK	= 0x3F<<FCR_RXTL_SHIFT,

	USR1	= 0x94/4,	/* UART Status Register 1 */
		SR1_PARITYERR	= 1<<15,	/* Parity Error Interrupt Flag */
		SR1_RTSS	= 1<<14,	/* RTS_B Pin Status */
		SR1_TRDY	= 1<<13,	/* Transmitter Ready Interrupt / DMA Flag */
		SR1_RTSD	= 1<<12,	/* RTS Delta */
		SR1_ESCF	= 1<<11,	/* Escape Sequence Interrupt Flag */
		SR1_FRAMEERR	= 1<<10,	/* Frame Error Interrupt Flag */
		SR1_RRDY	= 1<<9,		/* Receiver Ready Interrupt / DMA Flag */
		SR1_AGTIM	= 1<<8,		/* Aging Timer Interrupt Flag */
		SR1_DTRD	= 1<<7,	
		SR1_RXDS	= 1<<6,		/* Receiver IDLE Interrupt Flag */
		SR1_AIRINT	= 1<<5,		/* Asynchronous IR WAKE Interrupt Flag */
		SR1_AWAKE	= 1<<4,		/* Asynchronous WAKE Interrupt Flag */
		SR1_SAD		= 1<<3,		/* RS-485 Slave Address Detected Interrupt Flag */

	USR2	= 0x98/4,	/* UART Status Register 2 */
		SR2_ADET	= 1<<15,	/* Automatic Baud Rate Detected Complete */
		SR2_TXFE	= 1<<14,	/* Transmit Buffer FIFO Empty */
		SR2_DTRF	= 1<<13,
		SR2_IDLE	= 1<<12,	/* Idle Condition */
		SR2_ACST	= 1<<11,	/* Autobaud Counter Stopped */
		SR2_RIDELT	= 1<<10,
		SR2_RIIN	= 1<<9,
		SR2_IRINT	= 1<<8,		/* Serial Infrared Interrupt Flag */
		SR2_WAKE	= 1<<7,		/* Wake */
		SR2_DCDDELT	= 1<<6,
		SR2_DCDIN	= 1<<5,
		SR2_RTSF	= 1<<4,		/* RTS Edge Triggered Interrupt Flag */
		SR2_TXDC	= 1<<3,		/* Transmitter Complete */
		SR2_BRCD	= 1<<2,		/* BREAK Condition Detected */
		SR2_ORE		= 1<<1,		/* Overrun Error */
		SR2_RDR		= 1<<0,		/* Receive Data Ready */

	UESC	= 0x9C/4,	/* UART Escape Character Register */
	UTIM	= 0xA0/4,	/* UART Escape Timer Register */
	UBIR	= 0xA4/4,	/* UART BRM Incremental Modulator Register */
	UBMR	= 0xA8/4,	/* UART BRM Modulator Register */
	UBRC	= 0xAC/4,	/* UART Baud Rate Count Register */
	ONEMS	= 0xB0/4,	/* UART One-Millisecond Register */
	UTS	= 0xB5/4,	/* UART Test Register */
	UMCR	= 0xB8/4,	/* UART RS-485 Mode Control Register */
};

extern PhysUart imxphysuart;

static Uart uart1 = {
	.regs	= (u32int*)(VIRTIO + 0x860000ULL),
	.name	= "uart1",
	.baud	= 115200,
	.freq	= 25*Mhz,
	.phys	= &imxphysuart,
};

static Uart*
pnp(void)
{
	return &uart1;
}

static void
kick(Uart *u)
{
	u32int *regs = (u32int*)u->regs;

	while(u->op < u->oe || uartstageoutput(u)){
		if(u->blocked)
			break;
		if((regs[USR1] & SR1_TRDY) == 0){
			regs[UCR1] |= CR1_TRDYEN;
			return;
		}
		regs[UTXD] = *(u->op++) & TX_DATA;
	}
	regs[UCR1] &= ~CR1_TRDYEN;
}

static void
config(Uart *u)
{
	u32int cr2, *regs = u->regs;

	/* enable uart */
	regs[UCR1] = CR1_UARTEN;

	cr2 = CR2_SRST | CR2_IRTS | CR2_RXEN | CR2_TXEN;
	switch(u->parity){
	case 'e': cr2 |= CR2_PREN | CR2_PREVEN; break;
	case 'o': cr2 |= CR2_PREN | CR2_PRODD; break;
	}
	cr2 |= u->bits == 7 ? CR2_WS7 : CR2_WS8;
	if(u->stop == 2) cr2 |= CR2_STPB;
	regs[UCR2] = cr2;
	regs[UCR3] = 0x7<<8 | CR3_RXDMUXSEL;
	regs[UCR4] = 31<<CR4_CTSTL_SHIFT;

	/* baud = clock / (16 * (ubmr+1)/(ubir+1)) */
	regs[UFCR] = (6 - 1)<<FCR_RFDIV_SHIFT | 16<<FCR_TXTL_SHIFT | 1<<FCR_RXTL_SHIFT;
	regs[UBIR] = ((16*u->baud) / 1600)-1;
	regs[UBMR] = (u->freq / 1600)-1;

	regs[UCR1] = CR1_UARTEN | CR1_RRDYEN;
}

static int
bits(Uart *u, int n)
{
	switch(n){
	case 8:
		break;
	case 7:
		break;
	default:
		return -1;
	}
	u->bits = n;
	config(u);
	return 0;
}

static int
stop(Uart *u, int n)
{
	switch(n){
	case 1:
		break;
	case 2:
		break;
	default:
		return -1;
	}
	u->stop = n;
	config(u);
	return 0;
}

static int
parity(Uart *u, int n)
{
	switch(n){
	case 'n':
		break;
	case 'e':
		break;
	case 'o':
		break;
	default:
		return -1;
	}
	u->parity = n;
	config(u);
	return 0;
}

static int
baud(Uart *u, int n)
{
	if(u->freq == 0 || n <= 0)
		return -1;
	u->baud = n;
	config(u);
	return 0;
}

static void
rts(Uart*, int)
{
}

static void
dobreak(Uart*, int)
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
interrupt(Ureg*, void *arg)
{
	Uart *uart = arg;
	u32int v, *regs = (u32int*)uart->regs;

	while((v = regs[URXD]) & RX_CHARRDY)
		uartrecv(uart, v & RX_DATA);

	uartkick(uart);
}

static void
clkenable(Uart *u, int on)
{
	char clk[32];

	snprint(clk, sizeof(clk), "%s.ipg_perclk", u->name);
	if(on) setclkrate(clk, "osc_25m_ref_clk", u->freq);
	setclkgate(clk, on);
}

static void
disable(Uart *u)
{
	u32int *regs = u->regs;

	if(u->console)
		return;	/* avoid glitch */

	regs[UCR1] = 0;
	clkenable(u, 0);
}

static void
enable(Uart *u, int ie)
{
	disable(u);
	clkenable(u, 1);

	if(ie) intrenable(IRQuart1, interrupt, u, BUSUNKNOWN, u->name);

	config(u);
}

static void
donothing(Uart*, int)
{
}

static void
putc(Uart *u, int c)
{
	u32int *regs = u->regs;

	while((regs[USR1] & SR1_TRDY) == 0)
		;
	regs[UTXD] = c & TX_DATA;
}

static int
getc(Uart *u)
{
	u32int c, *regs = (u32int*)u->regs;

	do 
		c = regs[URXD];
	while((c & RX_CHARRDY) == 0);
	return c & RX_DATA;
}

void
uartconsinit(void)
{
	consuart = &uart1;
	consuart->console = 1;
	uartctl(consuart, "l8 pn s1");
}

PhysUart imxphysuart = {
	.name		= "imx",
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

