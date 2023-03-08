/*
 *  various things to IO with
 */

#define	IO(t,x)		((t*)(KSEG1|((ulong)x)))

/* for mt7688 testing on onion Ω 2 + */
#define	SYSCTLBASE	0x10000000
#define TIMERBASE	0x10000100
#define IRQBASE		0x10000200
#define MEMCBASE	0x10000300
#define RBUSBASE	0x10000400
#define	MCNTBASE	0x10000500
#define GPIOBASE	0x10000600
#define	I2CBASE		0x10000900
#define I2SBASE		0x10000A00
#define SPIBASE		0x10000B00
#define UARTLBASE	0x10000C00
#define UART1BASE	0x10000D00
#define UART2BASE	0x10000E00

#define	DMABASE		0x10002800
#define AESBASE		0x10004000	/* crypto engine */

#define ETHBASE		0x10100000
#define SWCHBASE	0x10110000
#define	PCIBASE		0x10140000
#define PCIWIN		0x10150000
#define	WIFIBASE	0x10300000
#define USBBASE		0x101C0000



/*
 *  duarts, frequency and registers
 */
#define DUARTFREQ	40000000  /* mt7688 has a 40MHz clock */	

#define UART_RBR	0x00
#define UART_THR	0x00
#define	UART_IER	0x04
#define UART_IIR	0x08
#define	UART_FCR	0x08
#define	UART_LCR	0x0C
#define UART_MCR	0x10
#define UART_LSR	0x14
#define	UART_MSR	0x18
#define	UART_SCR	0x1C
#define UART_DLL	0x00
#define UART_DLM	0x04


/*
 *	system control
 */

#define SYSCTL_RST			0x34


/*
 *  interrupt levels
 */

#define IRQshift	8;

/* for cpu */
enum {
	IRQsw1		=	0,	//INTR0
	IRQsw2,
	IRQlow,				//INTR2
	IRQhigh,
	IRQpci,
	IRQethr,
	IRQwifi,
	IRQtimer,			//INTR7
	IRQinc0,				// psuedo numbers for INC
	IRQsys,
	IRQtimer0,
	IRQillacc,
	IRQpcm,
	IRQinc5,
	IRQgpio,
	IRQdma,
	IRQinc8,
	IRQinc9,
	IRQi2s,
	IRQuartf,
	IRQspi,
	IRQcrypto,
	IRQnand,
	IRQperf,
	IRQinc16,
	IRQethsw,
	IRQusbh,
	IRQusbd,
	IRQuartl,
	IRQuart1,
	IRQuart2,
	IRQwdog,
	IRQmax,
};


/*
 * Interrupts on side controller
 */

#define INC_SYSCTL		1
#define INC_TIMER0		2
#define INC_ILLACC		3
#define INC_PCM			4

#define INC_GPIO		6
#define INC_DMA			7
#define INC_I2S			10
#define	INC_UARTF		11
#define INC_SPI			12 //?
#define INC_CRYPTO		13 //?
#define INC_NAND		14
#define INC_PERF		15
#define INC_ETHSW		17
#define	INC_USBH		18
#define	INC_USBD		19
#define	INC_UARTL		20
#define	INC_UART1		21
#define	INC_UART2		22
#define INC_WDOG		24

#define INC_GLOBAL		31



//#define INC_SDHC		14 //?
//#define INC_R2P			15 //?



/*
 * Interrupt Controller Registers
 */

#define IRQ_STAT		0x9C
#define FIQ_STAT		0xA0
#define IRQ_SEL0		0x00	/* set as IRQ */
#define	FIQ_SEL			0x6C	/* set as FIQ */
#define INT_PURE		0xA4	/* raw */
#define	IRQ_MASK		0x70	/* mask */
#define IRQ_MASK_SET	0x80	/* enable */
#define IRQ_MASK_CLR	0x78	/* disable */
#define	IRQ_EOI			0x88	/* call end to irq */


/*
 * timer controls
 */

#define TIME_GLB	0x00

#define CLK0_CTL	0x10
#define CLK0_LOAD	0x14
#define CLK0_TIME	0x18

#define WDOG_CTL	0x20
#define WDOG_LOAD	0x24
#define WDOG_TIME	0x28

#define GLB_T0_IRQ  (1<<0)
#define GLB_WD_IRQ	(1<<1)
#define GLB_T1_IRQ	(1<<2)
#define GLB_T0_RST	(1<<8)
#define	GLB_WD_RST	(1<<9)
#define	GLB_T1_RST	(1<<10)

#define TIMER_EN	(1<<7)  /* used on X_CTL regs */
#define AUTOLOAD	(1<<4)
#define CLK_PRSC(x)	((x)<<16)


/* for MIPS CNT */
#define MCNT_CFG	0x00
#define MCNT_CMP	0x04
#define	MCNT_CNT	0x08

#define MCNT_EN		1	/* for MCNT_CFG */


/* Frame Engine, ethernet controller */

#define TX_BASE_PTR_0	0x800	/*  TX Ring #0 Base Pointer */
#define	TX_MAX_CNT_0	0x804	/*  TX Ring #0 Maximum Count */
#define TX_CTX_IDX_0	0x808	/*  TX Ring #0 CPU pointer */
#define TX_DTX_IDX_0	0x80c	/*  TX Ring #0 DMA pointer */
#define PDMA_TX0_PTR	TX_BASE_PTR_0
#define PDMA_TX0_COUNT	TX_MAX_CNT_0
#define PDMA_TX0_CPU_IDX	TX_CTX_IDX_0
#define PDMA_TX0_DMA_IDX	TX_DTX_IDX_0
#define TX_BASE_PTR_1	0x810	/*  TX Ring #1 Base Pointer */
#define TX_MAX_CNT_1	0x814	/*  TX Ring #1 Maximum Count */
#define TX_CTX_IDX_1	0x818	/*  TX Ring #1 CPU pointer */
#define TX_DTX_IDX_1	0x81c	/*  TX Ring #1 DMA pointer */
#define TX_BASE_PTR_2	0x820	/*  TX Ring #2 Base Pointer */
#define TX_MAX_CNT_2	0x824	/*  TX Ring #2 Maximum Count */
#define TX_CTX_IDX_2	0x828	/*  TX Ring #2 CPU pointer */
#define TX_DTX_IDX_2	0x82c	/*  TX Ring #2 DMA pointer */
#define TX_BASE_PTR_3	0x830	/*  TX Ring #3 Base Pointer */
#define TX_MAX_CNT_3	0x834	/*  TX Ring #3 Maximum Count */
#define TX_CTX_IDX_3	0x838	/*  TX Ring #3 CPU pointer */
#define TX_DTX_IDX_3	0x83c	/*  TX Ring #3 DMA pointer */
#define RX_BASE_PTR_0	0x900	/*  RX Ring #0 Base Pointer */
#define RX_MAX_CNT_0	0x904	/*  RX Ring #0 Maximum Count */
#define RX_CRX_IDX_0	0x908	/*  RX Ring #0 CPU pointer */
#define RX_DRX_IDX_0	0x90c	/*  RX Ring #0 DMA pointer */
#define PDMA_RX0_PTR	RX_BASE_PTR_0
#define PDMA_RX0_COUNT	RX_MAX_CNT_0
#define PDMA_RX0_CPU_IDX	RX_CRX_IDX_0
#define PDMA_RX0_DMA_IDX	RX_DRX_IDX_0
#define RX_BASE_PTR_1	0x910	/*  RX Ring #1 Base Pointer */
#define RX_MAX_CNT_1	0x914	/*  RX Ring #1 Maximum Count */
#define RX_CRX_IDX_1	0x918	/*  RX Ring #1 CPU pointer */
#define RX_DRX_IDX_1	0x91c	/*  RX Ring #1 DMA pointer */
#define PDMA_INFO		0xa00	/*  PDMA Information */
#define PDMA_GLOBAL_CFG	0xa04	/*  PDMA Global Configuration */
#define	PDMA_IDX_RST	0xa08	/*	ring index reset ? */
#define DELAY_INT_CFG	0xa0c	/*  Delay Interrupt Configuration */
#define FREEQ_THRES		0xa10	/*  Free Queue Threshold */
#define INT_STATUS		0xa20	/*  Interrupt Status */
#define INT_MASK		0xa28	/*  Interrupt Mask */
#define PDMA_SCH		0xa80	/*  Scheduler Configuration for Q0&Q1 */
#define PDMA_WRR		0xa84	/*  Scheduler Configuration for Q2&Q3 */
#define SDM_CON			0xc00	/*  Switch DMA Control */
#define SDM_RING		0xc04	/*  Switch DMA Rx Ring */
#define SDM_TRING		0xc08	/*  Switch DMA TX Ring */
#define SDM_MAC_ADRL	0xc0c	/*  Switch MAC Address LSB */
#define SDM_MAC_ADRH	0xc10	/*  Switch MAC Address MSB */
#define GDMA1_MAC_LSB	SDM_MAC_ADRL
#define GDMA1_MAC_MSB	SDM_MAC_ADRH
#define SDM_TPCNT		0xd00	/*  Switch DMA Tx Packet Count */
#define SDM_TBCNT		0xd04	/*  Switch DMA TX Byte Count */
#define SDM_RPCNT		0xd08	/*  Switch DMA RX Packet Count */
#define SDM_RBCNT		0xd0c	/*  Switch DMA RX Byte Count */
#define SDM_CS_ERR		0xd10	/*  Switch DMA RX Checksum Error */


/*
 * 10/100 Switch registers
 */
#define SW_ISR		0x00
#define SW_IMR		0x04
#define SW_FCT0		0x08
#define  SW_FCT0_FC_RLS_TH(x)	(((x) & 0xff) << 24)
#define  SW_FCT0_FC_SET_TH(x)	(((x) & 0xff) << 16)
#define  SW_FCT0_DROP_RLS_TH(x)	(((x) & 0xff) << 8)
#define  SW_FCT0_DROP_SET_TH(x)	(((x) & 0xff) << 0)
#define SW_FCT1		0x0C
#define  SW_FCT1_PORT_TH(x)	(((x) & 0xff) << 0)
#define SW_PFC0		0x10
#define SW_PFC1		0x14
#define SW_PFC2		0x18
#define SW_QCS0		0x1C
#define SW_QCS1		0x20
#define SW_ATS		0x24
#define	SW_ATS0		0x28
#define SW_ATS1		0x2C
#define	SW_ATS2		0x30
#define SW_WMAD0	0x34
#define SW_WMAD1	0x38
#define SW_WMAD2	0x3C
#define SW_PVIDC0	0x40
#define SW_PVIDC1	0x44
#define SW_PVIDC2	0x48
#define SW_PVIDC3	0x4C
#define SW_VLANI0	0x50
#define SW_VLANI1	0x54
#define SW_VLANI2	0x58
#define SW_VLANI3	0x5C
#define SW_VLANI4	0x60
#define SW_VLANI5	0x64
#define SW_VLANI6	0x68
#define SW_VLANI7	0x6C
#define SW_VMSC0	0x70
#define SW_VMSC1	0x74
#define SW_VMSC2	0x78
#define SW_VMSC3	0x7C
#define SW_POA		0x80
#define SW_FPA		0x84
#define SW_PTS		0x88
#define SW_SOCPC	0x8C
#define SW_POC0		0x90
#define SW_POC1		0x94
#define SW_POC2		0x98
#define SW_SWGC		0x9C
#define SW_RST		0xA0
#define SW_LEDP0	0xA4
#define SW_LEDP1	0xA8
#define SW_LEDP2	0xAC
#define SW_LEDP3	0xB0
#define SW_LEDP4	0xB4
#define SW_WDOG		0xB8
#define SW_DBG		0xBC
#define SW_PCTL0	0xC0	/* PCR0 */
#define SW_PCTL1	0xC4	/* PCR1 */
#define SW_FPORT	0xC8
#define SW_FCT2		0xCC
#define SW_QSS0		0xD0
#define SW_QSS1		0xD4
#define SW_DBGC		0xD8
#define SW_MTI1		0xDC
#define SW_PPC		0xE0
#define SW_SGC2		0xE4
#define SW_PCNT0	0xE8
#define SW_PCNT1	0xEC
#define SW_PCNT2	0xF0
#define SW_PCNT3	0xF4
#define SW_PCNT4	0xF8
#define SW_PCNT5	0xFC


