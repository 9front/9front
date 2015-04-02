enum {
	Mhz		= 1000*1000,
};

#define	IO(t,x)		((t*)(KSEG1|((ulong)x)))

/* Interrupts */
#define IRQGIO0		0
#define IRQSCSI		1
#define IRQSCSI1	2
#define IRQENET		3
#define IRQGDMA		4
#define IRQPLP		5
#define IRQGIO1		6
#define IRQLCL2		7
#define IRQISDN_ISAC	8
#define IRQPOWER	9
#define IRQISDN_HSCX	10
#define IRQLCL3		11
#define IRQHPCDMA	12
#define IRQACFAIL	13
#define IRQVIDEO	14
#define IRQGIO2		15
#define IRQEISA		19
#define IRQKBDMS	20
#define IRQDUART	21
#define IRQDRAIN0	22
#define IRQDRAIN1	23
#define IRQGIOEXP0	22
#define IRQGIOEXP1	23

/*
 * Local Interrupt registers (INT2)
 */
#define	INT2_IP20	0x1fb801c0
#define	INT2_IP22	0x1fbd9000
#define	INT2_IP24	0x1fbd9880

#define INT2_BASE	INT2_IP24	/* indy */

#define	LIO_0_ISR	(INT2_BASE+0x3)
#define	LIO_0_MASK	(INT2_BASE+0x7)
#define	LIO_1_ISR	(INT2_BASE+0xb)
#define	LIO_1_MASK	(INT2_BASE+0xf)
#define	LIO_2_ISR	(INT2_BASE+0x13)
#define	LIO_2_MASK	(INT2_BASE+0x17)

#define	HPC3_ETHER	0x1fb80000
#define	HPC3_KBDMS	0x1fbd9800
#define	GIO_NEWPORT	0x1f0f0000	/* indy */

#define	MEMCFG0		0x1fa000c4	/* mem. size config. reg. 0 (w, rw) */
#define	MEMCFG1		0x1fa000cc	/* mem. size config. reg. 1 (w, rw) */

